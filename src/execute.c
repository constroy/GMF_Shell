#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <glob.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/termios.h>

#include "global.h"
#define DEBUG
int goon = 0, ingnore = 0;       //用于设置signal信号量
char *envPath[10], cmdBuff[40];  //外部命令的存放路径及读取外部命令的缓冲空间
History history;                 //历史命令
Job *head = NULL;                //作业头指针
pid_t fgPid,fgCid[100];          //当前前台作业的进程号
int cidCnt;
/*******************************************************
                  工具以及辅助方法
********************************************************/
/*判断命令是否存在*/
int exists(char *cmdFile){
    int i = 0;
    if((cmdFile[0] == '/' || cmdFile[0] == '.') && access(cmdFile, F_OK) == 0){ //命令在当前目录
        strcpy(cmdBuff, cmdFile);
        return 1;
    }else{  //查找ysh.conf文件中指定的目录，确定命令是否存在
        while(envPath[i] != NULL){ //查找路径已在初始化时设置在envPath[i]中
            strcpy(cmdBuff, envPath[i]);
            strcat(cmdBuff, cmdFile);
            
            if(access(cmdBuff, F_OK) == 0){ //命令文件被找到
                return 1;
            }
            
            i++;
        }
    }
    
    return 0; 
}


/*将字符串转换为整型的Pid*/
int str2Pid(char *str, int start, int end){
    int i, j;
    char chs[20];
    
    for(i = start, j= 0; i < end; i++, j++){
        if(str[i] < '0' || str[i] > '9'){
            return -1;
        }else{
            chs[j] = str[i];
        }
    }
    chs[j] = '\0';
    
    return atoi(chs);
}


/*调整部分外部命令的格式*/
void justArgs(char *str){
    int i, j, len;
    len = strlen(str);
    
    for(i = 0, j = -1; i < len; i++){
        if(str[i] == '/'){
            j = i;
        }
    }

    if(j != -1){ //找到符号'/'
        for(i = 0, j++; j < len; i++, j++){
            str[i] = str[j];
        }
        str[i] = '\0';
    }
}

/*设置goon*/
void setGoon(){
    goon = 1;
}

/*释放环境变量空间*/
void release(){
    int i;
    for(i = 0; strlen(envPath[i]) > 0; i++){
        free(envPath[i]);
    }
}

/*******************************************************
                  信号以及jobs相关
********************************************************/
/*添加新的作业*/
Job* addJob(pid_t pid){
    Job *now = NULL, *last = NULL, *job = (Job*)malloc(sizeof(Job));
    
	//初始化新的job
    job->pid = pid;
    strcpy(job->cmd, inputBuff);
    strcpy(job->state, RUNNING);
    job->next = NULL;
    
    if(head == NULL){ //若是第一个job，则设置为头指针
        head = job;
    }else{ //否则，根据pid将新的job插入到链表的合适位置
		now = head;
		while(now != NULL && now->pid < pid){
			last = now;
			now = now->next;
		}
        last->next = job;
        job->next = now;
    }
    
    return job;
}

/*移除一个作业*/
void rmJob(int sig, siginfo_t *sip, void* noused){
    pid_t pid;
    Job *now = NULL, *last = NULL;
    
    if(ingnore == 1){
        ingnore = 0;
        return;
    }
    
    pid = sip->si_pid;

    now = head;
	while(now != NULL && now->pid < pid){
		last = now;
		now = now->next;
	}
    
    if(now == NULL){ //作业不存在，则不进行处理直接返回
        return;
    }
    
	//开始移除该作业
    if(now == head){
        head = now->next;
    }else{
        last->next = now->next;
    }
    
    free(now);
}

/*组合键命令ctrl+z*/
void ctrl_Z(){
    Job *now = NULL;
    
    if(fgPid == 0){ //前台没有作业则直接返回
        return;
    }
    
    //SIGCHLD信号产生自ctrl+z
    ingnore = 1;
    
	now = head;
	while(now != NULL && now->pid != fgPid)
		now = now->next;
    
    if(now == NULL){ //未找到前台作业，则根据fgPid添加前台作业
        now = addJob(fgPid);
    }
    
	//修改前台作业的状态及相应的命令格式，并打印提示信息
    strcpy(now->state, STOPPED); 
    now->cmd[strlen(now->cmd)] = '&';
    now->cmd[strlen(now->cmd) + 1] = '\0';
    printf("[%d]\t%s\t\t%s\n", now->pid, now->state, now->cmd);
    
	//发送SIGSTOP信号给正在前台运作的工作，将其停止
    kill(fgPid, SIGTSTP);
    sleep(1);
    kill(fgPid, SIGSTOP);
    fgPid = 0;
}

///////////////////////////////////////////////////////////////////////////////////

/*组合键命令ctrl+c*/
void ctrl_C(){
    if(fgPid == 0){ //前台没有作业则直接返回
        return;
    }
    
    //SIGCHLD信号产生自ctrl+c
    ingnore = 1;
    
	//发送SIGSTOP信号给正在前台运作的工作，将其
    kill(fgPid, SIGKILL);
    fgPid = 0;
}

////////////////////////////////////////////////////////////////////////////////////////



/*fg命令*/
void fg_exec(int pid){    
    Job *now = NULL; 
	int i;
    
    //SIGCHLD信号产生自此函数
    ingnore = 1;
    
	//根据pid查找作业
    now = head;
	while(now != NULL && now->pid != pid)
		now = now->next;
    
    if(now == NULL){ //未找到作业
        printf("pid为7%d 的作业不存在！\n", pid);
        return;
    }

    //记录前台作业的pid，修改对应作业状态
    fgPid = now->pid;
    strcpy(now->state, RUNNING);
    
    signal(SIGTSTP, ctrl_Z); //设置signal信号，为下一次按下组合键Ctrl+Z做准备
    signal(SIGINT, ctrl_C); //设置signal信号，为下一次按下组合键Ctrl+c做准备(修改的)
    i = strlen(now->cmd) - 1;
    while(i >= 0 && now->cmd[i] != '&')
		i--;
    now->cmd[i] = '\0';
    
    printf("%s %d\n", now->cmd,now->pid);

    kill(now->pid + 1, SIGCONT);//向对象作业发送SIGCONT信号，使其运行
    kill(now->pid, SIGUSR2);
    
    waitpid(fgPid, NULL, 0); //父进程等待前台进程的运行
}

/*bg命令*/
void bg_exec(int pid){
    Job *now = NULL;
    
    //SIGCHLD信号产生自此函数
    ingnore = 1;
    
	//根据pid查找作业
	now = head;
    while(now != NULL && now->pid != pid)
		now = now->next;
    
    if(now == NULL){ //未找到作业
        printf("pid为7%d 的作业不存在！\n", pid);
        return;
    }
    
    strcpy(now->state, RUNNING); //修改对象作业的状态
    printf("[%d]\t%s\t\t%s\n", now->pid, now->state, now->cmd);

    kill(now->pid + 1, SIGCONT);//向对象作业发送SIGCONT信号，使其运行
    kill(now->pid, SIGUSR2);
    
}

/*******************************************************
                    命令历史记录
********************************************************/
void addHistory(char *cmd){
    if(history.end == -1){ //第一次使用history命令
        history.end = 0;
        strcpy(history.cmds[history.end], cmd);
        return;
	}
    
    history.end = (history.end + 1)%HISTORY_LEN; //end前移一位
    strcpy(history.cmds[history.end], cmd); //将命令拷贝到end指向的数组中
    
    if(history.end == history.start){ //end和start指向同一位置
        history.start = (history.start + 1)%HISTORY_LEN; //start前移一位
    }
}

/*******************************************************
                     初始化环境
********************************************************/
/*通过路径文件获取环境路径*/
void getEnvPath(int len, char *buf){
    int i, j, pathIndex = 0, temp;
    char path[40];
    
    for(i = 0, j = 0; i < len; i++){
        if(buf[i] == ':'){ //将以冒号(:)分隔的查找路径分别设置到envPath[]中
            if(path[j-1] != '/'){
                path[j++] = '/';
            }
            path[j] = '\0';
            j = 0;
            
            temp = strlen(path);
            envPath[pathIndex] = (char*)malloc(sizeof(char) * (temp + 1));
            strcpy(envPath[pathIndex], path);
            
            pathIndex++;
        }else{
            path[j++] = buf[i];
        }
    }
    
    envPath[pathIndex] = NULL;
}

/*初始化操作*/
void init(){
    int fd, len;
    char c, buf[80];

	//打开查找路径文件ysh.conf
    if((fd = open("ysh.conf", O_RDONLY, 660)) == -1){
        perror("init environment failed\n");
        exit(1);
    }
    
	//初始化history链表
    history.end = -1;
    history.start = 0;
    
    len = 0;
	//将路径文件内容依次读入到buf[]中
    while(read(fd, &c, 1) != 0){ 
        buf[len++] = c;
    }
    buf[len] = '\0';

    //将环境路径存入envPath[]
    getEnvPath(len, buf); 
    
    //注册信号
    struct sigaction action;
    action.sa_sigaction = rmJob;
    sigfillset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    sigaction(SIGCHLD, &action, NULL);
    signal(SIGTSTP, ctrl_Z);
    signal(SIGINT, ctrl_C);
    
}


/*******************************************************
                      命令解析
********************************************************/
SimpleCmd* handleSimpleCmdStr(int begin, int end){
    int i, j, k;
    int fileFinished; //记录命令是否解析完毕
    char buff[10][40], inputFile[30], outputFile[30], *temp = NULL;
    SimpleCmd *cmd = (SimpleCmd*)malloc(sizeof(SimpleCmd));
    
	//默认为非后台命令，输入输出重定向为null
    cmd->isBack = 0;
    cmd->input = cmd->output = NULL;
    
    //初始化相应变量
    for(i = begin; i<10; i++){
        buff[i][0] = '\0';
    }
    inputFile[0] = '\0';
    outputFile[0] = '\0';
    
    i = begin;
	//跳过空格等无用信息
    while(i < end && (inputBuff[i] == ' ' || inputBuff[i] == '\t')){
        i++;
    }
    
    k = 0;
    j = 0;
    fileFinished = 0;
    temp = buff[k]; //以下通过temp指针的移动实现对buff[i]的顺次赋值过程
    while(i < end){
		/*根据命令字符的不同情况进行不同的处理*/
        switch(inputBuff[i]){ 
            case ' ':
            case '\t': //命令名及参数的结束标志
                temp[j] = '\0';
                j = 0;
                if(!fileFinished){
                    k++;
                    temp = buff[k];
                }
                break;

            case '<': //输入重定向标志
                if(j != 0){
		    //此判断为防止命令直接挨着<符号导致判断为同一个参数，如果ls<sth
                    temp[j] = '\0';
                    j = 0;
                    if(!fileFinished){
                        k++;
                        temp = buff[k];
                    }
                }
                temp = inputFile;
                fileFinished = 1;
                i++;
                break;
                
            case '>': //输出重定向标志
                if(j != 0){
                    temp[j] = '\0';
                    j = 0;
                    if(!fileFinished){
                        k++;
                        temp = buff[k];
                    }
                }
                temp = outputFile;
                fileFinished = 1;
                i++;
                break;
                
            case '&': //后台运行标志
                if(j != 0){
                    temp[j] = '\0';
                    j = 0;
                    if(!fileFinished){
                        k++;
                        temp = buff[k];
                    }
                }
                cmd->isBack = 1;
                fileFinished = 1;
                i++;
                break;
                
            default: //默认则读入到temp指定的空间
                temp[j++] = inputBuff[i++];
                continue;
		}
        
		//跳过空格等无用信息
        while(i < end && (inputBuff[i] == ' ' || inputBuff[i] == '\t')){
            i++;
        }
	}
    
    if(inputBuff[end-1] != ' ' && inputBuff[end-1] != '\t' && inputBuff[end-1] != '&'){
        temp[j] = '\0';
        if(!fileFinished){
            k++;
        }
    }
 //==================================================================================   
	//依次为命令名及其各个参数赋值
    // cmd->args = (char**)malloc(sizeof(char*) * (k + 1));
    // cmd->args[k] = NULL;
    // for(i = 0; i<k; i++){
    //     j = strlen(buff[i]);
    //     cmd->args[i] = (char*)malloc(sizeof(char) * (j + 1));   
    //     strcpy(cmd->args[i], buff[i]);
    // }

    glob_t gl;    gl.gl_offs = 0;

    for(i=0; i<k; i++)              // Acquare the length of cmd->args
    {
        if(i == 0)
        	glob(buff[i], GLOB_NOCHECK | GLOB_DOOFFS, NULL, &gl);
        else
        	glob(buff[i], GLOB_NOCHECK | GLOB_DOOFFS | GLOB_APPEND, NULL, &gl);
    }

    cmd->args = (char**)malloc(sizeof(char*) * (gl.gl_pathc + 1));    //malloc a cmd->args arrording to total
    cmd->args[gl.gl_pathc] = NULL;
    for(i=0; i<gl.gl_pathc; i++)              //The number of gl[] is gl.gl_pathc
    {
        j = strlen(gl.gl_pathv[i]);
        cmd->args[i] = (char*)malloc(sizeof(char) * (j + 1));
        strcpy(cmd->args[i], gl.gl_pathv[i]);
    }
    globfree(&gl);
 //==================================================================================  

	//如果有输入重定向文件，则为命令的输入重定向变量赋值
    if(strlen(inputFile) != 0){
        j = strlen(inputFile);
        cmd->input = (char*)malloc(sizeof(char) * (j + 1));
        strcpy(cmd->input, inputFile);
    }

    //如果有输出重定向文件，则为命令的输出重定向变量赋值
    if(strlen(outputFile) != 0){
        j = strlen(outputFile);
        cmd->output = (char*)malloc(sizeof(char) * (j + 1));   
        strcpy(cmd->output, outputFile);
    }
    #ifdef DEBUG
    printf("****\n");
    printf("isBack: %d\n",cmd->isBack);
    	for(i = 0; cmd->args[i] != NULL; i++){
    		printf("args[%d]: %s\n",i,cmd->args[i]);
	}
    printf("input: %s\n",cmd->input);
    printf("output: %s\n",cmd->output);
    printf("****\n");
    #endif
    return cmd;
}

/*******************************************************
                      命令执行
********************************************************/
/*执行外部命令*/
void execOuterCmd(SimpleCmd *cmd){
    int pipeIn, pipeOut;
    
    if(exists(cmd->args[0]) == 0){ //命令不存在
		fprintf(stderr, "找不到命令 %s\n", inputBuff);
		exit(errno);
	}
	
	if(cmd->input != NULL){ //存在输入重定向
		if((pipeIn = open(cmd->input, O_RDONLY, S_IRUSR|S_IWUSR)) == -1){
			fprintf(stderr, "不能打开文件%s！\n", cmd->input);
			exit(errno);
		}
		if(dup2(pipeIn, 0) == -1){
			perror("重定向标准输入错误！");
			exit(errno);
		}
	}
	
	if(cmd->output != NULL){ //存在输出重定向
		if((pipeOut = open(cmd->output, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR)) == -1){
			fprintf(stderr, "不能打开文件%s！\n", cmd->output);
			exit(errno);
		}
		if(dup2(pipeOut, 1) == -1){
			perror("重定向标准输出错误！");
			exit(errno);
		}
	}
	
	justArgs(cmd->args[0]);
	if(execv(cmdBuff, cmd->args) < 0){ //执行命令
		perror("execv failed!");
		exit(errno);
	}
}

/*执行命令*/
void execSimpleCmd(SimpleCmd *cmd){
    int i, pid;
    char *temp;
    Job *now = NULL;
    
    if (strcmp(cmd->args[0], "history") == 0) { //history命令
        if(history.end == -1){
            printf("尚未执行任何命令\n");
            return;
        }
        i = history.start;
        do {
            printf("%s\n", history.cmds[i]);
            i = (i + 1)%HISTORY_LEN;
        } while(i != (history.end + 1)%HISTORY_LEN);
    } else if (strcmp(cmd->args[0], "jobs") == 0) { //jobs命令
        if(head == NULL){
            printf("尚无任何作业\n");
        } else {
            printf("index\tpid\tstate\t\tcommand\n");
            for(i = 1, now = head; now != NULL; now = now->next, i++){
                printf("%d\t%d\t%s\t\t%s\n", i, now->pid, now->state, now->cmd);
            }
        }
    } else if (strcmp(cmd->args[0], "cd") == 0) { //cd命令
        temp = cmd->args[1];
        if(temp != NULL){
            if(chdir(temp) < 0){
                printf("cd; %s 错误的文件名或文件夹名！\n", temp);
            }
        }
    } else if (strcmp(cmd->args[0], "fg") == 0) { //fg命令
        temp = cmd->args[1];
        if(temp != NULL && temp[0] == '%'){
            pid = str2Pid(temp, 1, strlen(temp));
            if(pid != -1){
                fg_exec(pid);
            }
        }else{
            printf("fg; 参数不合法，正确格式为：fg %%<int>\n");
        }
    } else if (strcmp(cmd->args[0], "bg") == 0) { //bg命令
        temp = cmd->args[1];
        if(temp != NULL && temp[0] == '%'){
            pid = str2Pid(temp, 1, strlen(temp));
            
            if(pid != -1){
                bg_exec(pid);
            }
        }
		else{
            printf("bg; 参数不合法，正确格式为：bg %%<int>\n");
        }
    } else{ //外部命令
        execOuterCmd(cmd);
    }
    
    //释放结构体空间
    for(i = 0; cmd->args[i] != NULL; i++){
        free(cmd->args[i]);
        free(cmd->input);
        free(cmd->output);
    }
}

//preprocess cmds by constroy
void *handleComplexCmdStr(int begin,int end) {
	int i,j,cnt;
	ComplexCmd *cmd = (ComplexCmd*)malloc(sizeof(ComplexCmd));
	cnt = 0;
	for (i = begin; i<end; ++i) {
		cnt += (inputBuff[i] == '|');
	}
	cmd->num = ++cnt;
	cmd->cmds = (SimpleCmd**)malloc(cnt*sizeof(SimpleCmd*));

	cnt = 0;
	for (i = begin, j=begin; i<end && j<end; ++j) {
		if (inputBuff[j] == '|') {
			cmd->cmds[cnt++] = handleSimpleCmdStr(i,j);
			i = j + 1;
		}
	}
	cmd->cmds[cnt++] = handleSimpleCmdStr(i,j);
	cmd->isBack = cmd->cmds[cnt-1]->isBack;
	for (i = 0; i<cmd->num; ++i) {
		if(strcmp(cmd->cmds[i]->args[0], "exit") == 0) exit(0);//exit
		cmd->cmds[i]->isBack = cmd->isBack;
	}
	return cmd;
}
//execute a set of CMDs by constroy
void executeComplexCmd(ComplexCmd *cmd) {
	int i;
	pid_t pid;
	int pfd[2][2]={{0,1},{0,1}};
	
	cidCnt = 0;
	for (i = 0; i<cmd->num; ++i) {

		if (i == cmd->num - 1) {
			pfd[1][1] = 1;
		}
		else {
			if (pipe(pfd[1]) == -1) {
				perror("pipe failed");
				exit(errno);
			}
		}
		pid = fork();
		if (pid < 0) {
			perror("fork failed");
			exit(errno);
		} else if (pid) {
			fgCid[cidCnt++] = pid;
			if (pfd[0][0] != 0) close(pfd[0][0]);
			if (pfd[1][1] != 1) close(pfd[1][1]);
			pfd[0][0]=pfd[1][0];
		} else {
			dup2(pfd[0][0],0);
			dup2(pfd[1][1],1);
			execSimpleCmd(cmd->cmds[i]);
			close(pfd[0][0]);
			close(pfd[1][1]);
			exit(0);
		}
	}
	if (pfd[0][0] != 0) close(pfd[0][0]);
	//wait for all child processes to exit
	while ((pid=wait(NULL))>0);
	//free cmds[]
	for (i = 0; i<cmd->num; ++i) free(cmd->cmds[i]);
}
void stopFg()
{
	int i;
	for (i=0; i<cidCnt; ++i) kill(fgCid[i], SIGSTOP);
}
void contFg()
{
	int i;
	for (i=0; i<cidCnt; ++i) kill(fgCid[i], SIGCONT);
}
/*******************************************************
                     命令执行接口
********************************************************/
void execute(){
	ComplexCmd *cmd = handleComplexCmdStr(0,strlen(inputBuff));
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork failed");
		exit(errno);
    }
    if (pid) {
		if(cmd->isBack) { //后台命令             
			fgPid = 0; //pid置0，为下一命令做准备
			addJob(pid+1); //增加新的作业
			signal(SIGUSR1,setGoon);
			sleep(1);
			kill(pid,SIGUSR1); //子进程发信号，表示作业已加入
			//等待子进程
			while (goon == 0);
			goon = 0;
		} else { //非后台命令
			fgPid = pid;
			waitpid(pid,NULL,0);
		}
	} else {
		cidCnt = 0;
		
		if (cmd->isBack) {
			signal(SIGUSR1,setGoon);
			signal(SIGTSTP,NULL);
			signal(SIGINT,NULL);
			while (goon == 0);
			printf("[%d]\t%s\t\t%s\n", getpid(), RUNNING, inputBuff);
			kill(getppid(), SIGUSR1);
		} else {
			signal(SIGUSR2,contFg);
			signal(SIGTSTP,stopFg);
		}
		executeComplexCmd(cmd);
		exit(0);
	}
	//free
	free(cmd);
}
