*
  uterm Copyright 1996 Jani Jaakkola.
  Parts of this code comes from minicom 
  Copyright 1991-1995 Miquel van Smoorenburg
  */

#include <unistd.h>
#include <stdio.h>
#include <termio.h>
#include <termios.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <string.h>

/* 0.6: changed escape character */

#define VERSION "0.6 (5.1.2002)"

/* Escape character */
/* #define ESCAPECH 1 */ /* ^A just like minicom */
#define ESCAPECH '*' /*I prefer this one. Programs use control chars but
		      (almost) never the ones with 8-bit set */

#define BEEPCH 7
/* #define BEEPCH '?' */   /* Use this if you don't like beeps */

/* Default device */
#define DEFDEVICE ""
#define DEFLOCKDIR "/var/lock"

int speed=B9600;  /* Default baud rate (Could be B300,B1200,B2400 ... )*/
#define PARITY "8N1" /* 8-bits No parity,1 stop bit by default */

#define FLOW_NONE 0
#define FLOW_SOFT 1
#define FLOW_HARD 2

int flow_control=FLOW_HARD;  /* Default flow control */
int verbose=1; /* Write lines about what's happening */
int do_lock=0;

char *help_str=
"\r\n-----------------------------------------------------------------------"
"\r\n ' ': Do nothing   'F': Send break    'H': Hangup  'J': Suspend"
"\r\n 'Q': Quit         'X': Hangup & Quit 'Z': Print this message"
"\r\n-----------------------------------------------------------------------"
"\r\n";

int do_stty(int,struct termios *);
int term_to_raw(int, struct termios *save);
int modem_to_raw(int, struct termios *save);
int open_line(char *device);
int main_loop();
int do_command();
int send_break(int);
int hangup(int);
#define fail(X) do_fail(__LINE__,X)
void do_fail(int line, int);
void parse_options(int argc, char *argv[]);
int lock_device(char *);
void do_shutdown();
void catch_signals(void);
void deadly_signal(int);
void print_help();

struct termios tsave;
struct termios msave;
int mfd=-1; /* Terminal tty file descriptor */
int mraw=0; /* Is terminal tty in raw state */
int traw=0; /* Is user tty in raw state */
char beepch=BEEPCH;
char escapech=ESCAPECH;
char device[200]=DEFDEVICE;
char lockdir[200]=DEFLOCKDIR;
char lockfile[200]="";


int main(int argc,char *argv[]) {
	char escch[3];

	fprintf(stderr,"uterm "VERSION" by Jani Jaakkola (jjaakkol@cs.helsinki.fi)\n");
	fprintf(stderr,"use 'uterm -h' for help.\n");
	
	parse_options(argc,argv);
	if (!device[0]) {
		fprintf(stderr,"You have specify a serial device with -d option.\n");
		exit(1);
	}
	atexit(do_shutdown);	
	catch_signals();
	if (do_lock && lock_device(device)!=0) {
		exit(1);
	}
	mfd=open_line(device);
	
	if (escapech<32 && escapech>=0) {
		sprintf(escch,"^%c",escapech+'A'-1);
	} else {
		sprintf(escch,"%c",escapech);
	}
	fprintf(stderr,
		"Escape character is '%s'. Use '%s Z' for help on available commands.\n",
		escch,escch);

	term_to_raw(0,&tsave);
	traw=1;
		
	main_loop();
	
	do_stty(mfd,&msave);
	do_stty(0,&tsave);

	fprintf(stderr,"\nuterm is done.\n");
	return 0;
}


void parse_options(int argc, char *argv[]) {
	int i,j;

	struct speeds_s {
		char *name;
		int speed;
	} speeds[] = {
		{"50",B50}, {"75",B75}, {"110",B110}, {"134",B134},
		{"150",B150}, {"200",B200} , {"300",B300}, {"600",B600},
		{"1200",B1200}, {"1800",B1800}, {"2400",B2400}, 
		{"4800",B4800}, {"9600",B9600}, {"19200",B19200},
		{"19K",B19200}, {"38400",B38400}, {"38K",B38400},
		{"57600",B57600}, {"57K",B57600},{"115200",B115200},
		{"115K",B115200}, {"230400",B230400},
		{"230K",B230400},
		{NULL,0}
	};

	do {
		i=getopt(argc, argv,"lhvd:b:f:");
		switch (i) {
		case 'h':
			print_help();
			exit(0);
		case 'l':
			do_lock=!do_lock;
			break;
		case 'f':
		    if (!strcmp(optarg,"hard")) {
			flow_control=FLOW_HARD;
		    } else if (!strcmp(optarg,"soft")) {
			flow_control=FLOW_SOFT;
		    } else if (!strcmp(optarg,"none")) {
			flow_control=FLOW_NONE;
		    } else {
			fprintf(stderr,"Invalid flow control value '%s'\n",
				optarg);
			fprintf(stderr,"Must be soft, hard, or none\n");
			exit(1);
		    }
		    break;
		case 'd':
			if (optarg[0]=='/') {
				strcpy(device,optarg);
			} else {
				sprintf(device,"/dev/%s",optarg);
			}
			break;
		case 'b':
			for(j=0; 
			    speeds[j].name!=NULL && 
				    strcmp(speeds[j].name, optarg);j++);
			if (speeds[j].name==NULL) {
				fprintf(stderr,"Invalid speed '%s'\n",optarg);
				fprintf(stderr,"Must be one of:");
				for(j=0;speeds[j].name!=NULL;j++) {
					fprintf(stderr," %s",speeds[j].name);
				}
				fprintf(stderr,"\n");
				exit(1);
			}
			speed=speeds[j].speed;
			break;
		case 'q':
			verbose=0;
		case '?':
		case ':':
			fprintf(stderr,"use 'uterm -h' for help\n");
			exit(1);
		}
	} while (i!=-1);
}

void print_help() {
	fprintf(stderr,
"\nuterm is a small terminal program for Linux.\n"
"usage: uterm -b <speed> -d <device>\n"
"\nOptions are:\n"
"\t-h\t\t\tPrint sort help (this text)\n"
"\t-b <speed>\t\tSet baud rate (default 9600)\n"
"\t-d <device>\t\tSet modem device\n"
"\t-f soft|hard|none\tSelect flow control\n"
"\t-l\t\t\tCreate lock file in '%s'\n"
"\t-q\t\t\tQuiet mode (supress most messages)\n\n",lockdir);
}

char *mbasename(char *file) {
	int i;

	i=strlen(file);
	while (i>0 && file[i]!='/') i--;
	return file+i+1;
}

/* lock_device comes from minicom 1.72 by Miquel van Smoorenburg */
int lock_device(char *dev) {
	struct stat stt;
	char buf[127];
	pid_t pid;
	int n;
	int fd;
	char *username;

	/* I guess this is OK, since we are not suid root */
	username=getenv("USER");
	if (username==NULL) {
		fprintf(stderr,"Please set environment variable USER to your username\n");
		return -1;
	}
	/* First see if the lock file directory is present. */
	if (lockdir[0] && stat(lockdir, &stt) == 0)
		sprintf(lockfile, "%s/LCK..%s", lockdir, mbasename(dev));
	else {
		/* So, we have no file locking */
		fprintf(stderr,"Could not locate lock file directory '%s':\n\t%s(%d)\n",
			lockdir,strerror(errno),errno);
		return -1;
	}
	
	if ((fd = open(lockfile, O_RDONLY)) >= 0) {
		n = read(fd, buf, 127);
		close(fd);
		if (n > 0) {
			pid = -1;
			if (n == 4)
				/* Kermit-style lockfile. */
				pid = *(int *)buf;
			else {
				/* Ascii lockfile. */
				buf[n] = 0;
				sscanf(buf, "%d", &pid);
			}
			
			if (pid > 0 && kill((pid_t)pid, 0) < 0 && errno == ESRCH) {
				fprintf(stderr, "Lockfile is stale. Overriding it..\n");
				if (unlink(lockfile)<0) {
					fprintf(stderr,"Failed to unlink lockfile '%s':%s(%d)\n",
						lockfile,strerror(errno),errno);
					return -1;
				}
			} else {
				n = 0;
			}
		}
		if (n == 0) {
			fprintf(stderr, "Device %s is locked.\n", dev);
			return(1);
		}
	}
        if ((fd = open(lockfile, O_WRONLY | O_CREAT | O_EXCL, 0666)) < 0) {
                fprintf(stderr, "Cannot create lockfile '%s' : %s(%d)\n",
			lockfile,strerror(errno),errno);
                return(-1);
		
        }
	sprintf(buf, "%05d minicom %.20s\n", (int)getpid(), username);
        if (write(fd, buf, strlen(buf))!=strlen(buf)) {
		fprintf(stderr,"Failed to write lockfile '%s' : %s(%d)\n",
			lockfile,strerror(errno),errno);
                return(-1);
	}
	close(fd);
	return 0;
}

int open_line(char *device) {
	int fd1,fd2;

	fd1=open(device,O_NOCTTY | O_RDWR | O_NDELAY);
	if (fd1==-1) {
		fprintf(stderr,"open '%s' failed:%s\n",device, strerror(errno));
		exit(1);
	}
	modem_to_raw(fd1,&msave);
	mraw=1;
	fd2=open(device,O_NOCTTY|O_RDWR);
	if (fd2==-1) {
		fprintf(stderr,"reopen '%s' failed:%s\n",device,strerror(errno));
		exit(1);
	}	
	close(fd1);
	return fd2;
}

int main_loop() {
	fd_set rset;
	int e;
	char ch;

	do {
	        FD_ZERO(&rset);
		FD_SET(mfd,&rset);
		FD_SET(0,&rset);

		e=select(mfd+1,&rset,NULL,NULL,NULL);
		if (e<0) {
			fail(1);
		}
		if (FD_ISSET(0,&rset)) {
			e=read(0,&ch,1);
			if (e!=1) {
				fail(1);
			}
			if (ch==escapech) {
				if (do_command()) {
					return 0;
				}
			} else {
				e=write(mfd,&ch,1);
				if (e!=1) {
					fail(1);
				}
			}
		}
		if (FD_ISSET(mfd,&rset)) {
			e=read(mfd,&ch,1);
			if (e==0) {
				char *hangup="\r\n[HANGUP]\r\n";
				write(1,hangup,strlen(hangup));
				exit(0);
			} 
			if (e<0 || e>1) {
				fail(1);
			}
			if (e==1) {
				e=write(1,&ch,1);
				if (e!=1) {
					fail(1);
				}
			}
		}
	} while(1);
	return 0;
}

int do_command() {
	char ch;
	int e;

	e=read(0,&ch,1);
	if (e<=0) {
		fail(1);
	}
	
	if (ch<127 && ch>32) {
		ch=tolower(ch);
	}
	if (ch==escapech) {	
		if (write(mfd,&ch,1)!=1) {
			fail(1);
		}
	} else {		
		switch(ch) {
		case ' ':
			break;
		case 'f':
			send_break(mfd);
			break;
		case 'h':
			hangup(mfd);
			break;
		case 'j':
			do_stty(0,&tsave);
			raise(SIGTSTP);
			term_to_raw(0,&tsave);
			break;
		case 'q':
			return 1;
		case 'x':
			hangup(mfd);
			return 1;
		case 'z':
			write(1,help_str,strlen(help_str));
			do_command();
			break;
		default:
			ch=beepch;
			if (write(1,&ch,1)!=1) {
				fail(1);
			}
		}
	}
	return 0;
}

void catch_signals(void) {
	int i;
	void *e;
	return;
	for(i=1;i<NSIG;i++) {
		e=0;
		if ( i!=SIGUSR1 && i!=SIGUSR2 && i!=SIGALRM && i!=SIGWINCH &&
		     i!=SIGKILL && i!=SIGSTOP && i!=SIGTSTP && i!=SIGALRM &&
		     i!=SIGCONT )
			e=signal(i,deadly_signal);
		if (e==SIG_ERR) {
			perror("signal");
			exit(1);
		}
	}
}

void deadly_signal(int sig) {
	char str[200];
	sprintf(str,"\r\nuterm caught deadly signal: %s(%d)\r\n",
		sys_siglist[sig],sig);
	write(2,str,strlen(str));
	exit(0);
}

void do_fail(int line,int errorcode) {
	char str[200];

	str[0]=0;
	if (errno!=0) {
		sprintf(str,"Failure: %s(%d)\r\n",strerror(errno),errno);
	}
	if (str[0]!=0) fputs(str,stderr);
	exit(errorcode);
}

void do_shutdown() {
	if (mraw) {
		do_stty(mfd,&msave);
		mraw=0;
	}
	if (traw) {
		do_stty(0,&tsave);
		traw=0;
	}
	if (lockfile[0]!=0) {
		unlink(lockfile);
		lockfile[0]=0;
	}
}

int term_to_raw(int fd, struct termios *tsave) {
	struct termios ta;
	
	if ( tcgetattr(fd,&ta)==-1 ) {
		fprintf(stderr,"tcgetattr (terminal): %s\n",strerror(errno));
		fail(1);
	}
	*tsave=ta;
	/*	ta.c_iflag=0;
	ta.c_oflag=0;
	ta.c_lflag=0;
	*/
	cfmakeraw(&ta);
	if ( tcsetattr(fd,TCSANOW,&ta)==-1) {
		fail(1);
	}
	return 0;
}

/* Put terminal device to raw state */
int modem_to_raw(int tfd, struct termios *tsave)
{
	struct termios ta;
	
	if ( tcgetattr(tfd,&ta)==-1 )
		{
			fprintf(stderr,"tcgetattr (modem): %s\n",strerror(errno));
			fail(1);
		}
	*tsave=ta;
	/*
	ta.c_iflag=0;
	if (flow_control==FLOW_SOFT) {
	    ta.c_iflag |= IXON | IXOFF;
	}
	ta.c_oflag=0;
	ta.c_cflag=speed | CS8 | CLOCAL;
	if (flow_control==FLOW_HARD) {
	    ta.c_cflag |= CRTSCTS;
	}
	ta.c_lflag=0;
	ta.c_cc[VMIN]=1;
	ta.c_cc[VTIME]=5;
	*/
	cfmakeraw(&ta);
	if ( tcsetattr(tfd,TCSANOW,&ta)==-1 ){
		fail(1);
	}
	return 0;
}

int do_stty(int tfd, struct termios *ta) {
	if ( tcsetattr(tfd,TCSANOW,ta)==-1 )
		{
			perror("tcsetattr");
			return -1;
		}
	return 0;
}

int hangup(int fd) {
	struct termios ta;

	if (verbose) {
		fprintf(stderr,"\r\nHardware hangup..");
		fflush(stderr);
	}
	if (tcgetattr(fd,&ta)<0) {
		fail(1);
	}
	ta.c_cflag&=~CBAUD;
	if (tcsetattr(fd,TCSANOW,&ta)<0) {
		fail(1);
	}
	sleep(3);
	ta.c_cflag|=speed;
	cfsetospeed(&ta,speed);
	if (tcsetattr(fd,TCSANOW,&ta)<0) {
		fail(1);
	}
	if (verbose) {
		fprintf(stderr," OK\r\n");
		fflush(stderr);
	}
	return 0;
}

int send_break(int fd) {
	if (verbose) {
		fprintf(stderr,"\r\nSending break\r\n");
	}
	tcsendbreak(fd,0);
	return 0;
}





