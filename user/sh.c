// Shell.

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "kernel/fs.h"

// Parsed command representation
#define EXEC  1
#define REDIR 2
#define PIPE  3
#define LIST  4
#define BACK  5

#define MAXARGS 10

#define MAXLINE  100
#define MAXHIST  16
#define MAXMATCH 64

static char hist[MAXHIST][MAXLINE];
static int  hcount;
static int  interactive;

static void
addhistory(char *line)
{
  char *p;

  for(p = line; *p == ' ' || *p == '\t'; p++)
    ;
  if(*p == 0)
    return;
  strcpy(hist[hcount % MAXHIST], line);
  hcount++;
}

static void
printhist(void)
{
  int k, first = hcount > MAXHIST ? hcount - MAXHIST : 0;

  for(k = first; k < hcount; k++)
    printf("%d %s\n", k + 1, hist[k % MAXHIST]);
}

static int
expand_history(char *buf)
{
  int k = 0;
  char *p;

  if(buf[1] == '!' && buf[2] == 0){
    if(hcount == 0){
      fprintf(2, "!!: event not found\n");
      return -1;
    }
    k = hcount - 1;
  } else if(buf[1] >= '0' && buf[1] <= '9'){
    for(p = buf + 1; *p >= '0' && *p <= '9'; p++){
      k = k * 10 + (*p - '0');
      if(k > 100000)
        break;
    }
    if(*p != 0)
      return 0;
    k--;
    if(k < 0 || k >= hcount || k < hcount - MAXHIST){
      fprintf(2, "%s: event not found\n", buf);
      return -1;
    }
  } else
    return 0;

  strcpy(buf, hist[k % MAXHIST]);
  return 1;
}

static int
startswith(char *s, char *prefix)
{
  while(*prefix){
    if(*s++ != *prefix++)
      return 0;
  }
  return 1;
}

static int
findmatches(char *dir, char *base, char names[][DIRSIZ+1], int max)
{
  int fd, n = 0;
  struct dirent de;

  if((fd = open(dir, O_RDONLY)) < 0)
    return 0;
  while(n < max && read(fd, &de, sizeof(de)) == sizeof(de)){
    if(de.inum == 0)
      continue;
    memmove(names[n], de.name, DIRSIZ);
    names[n][DIRSIZ] = 0;
    if(strcmp(names[n], ".") == 0 || strcmp(names[n], "..") == 0)
      continue;
    if(!startswith(names[n], base))
      continue;
    n++;
  }
  close(fd);
  return n;
}

static int
complete(char *buf, int len)
{
  char names[MAXMATCH][DIRSIZ+1];
  char dir[MAXLINE];
  char tail[DIRSIZ+2];
  char *word, *base;
  int ws, i, n, first, slash;

  buf[len] = 0;

  ws = len;
  while(ws > 0 && buf[ws-1] != ' ' && buf[ws-1] != '\t')
    ws--;
  word = buf + ws;

  first = 1;
  for(i = 0; i < ws; i++)
    if(buf[i] != ' ' && buf[i] != '\t')
      first = 0;

  slash = -1;
  for(i = 0; word[i]; i++)
    if(word[i] == '/')
      slash = i;
  if(slash >= 0){
    if(slash == 0){
      strcpy(dir, "/");
    } else {
      memmove(dir, word, slash);
      dir[slash] = 0;
    }
    base = word + slash + 1;
  } else {
    strcpy(dir, first ? "/" : ".");
    base = word;
  }

  n = findmatches(dir, base, names, MAXMATCH);
  if(n == 0)
    return len;

  if(n == 1){
    strcpy(tail, names[0] + strlen(base));
    if(first)
      strcpy(tail + strlen(tail), " ");
    if(len + strlen(tail) >= MAXLINE - 1)
      return len;
    strcpy(buf + len, tail);
    write(2, tail, strlen(tail));
    return len + strlen(tail);
  }

  write(2, "\n", 1);
  for(i = 0; i < n; i++)
    fprintf(2, "%s%s", i ? " " : "", names[i]);
  fprintf(2, "\n$ %s", buf);
  return len;
}

static int
replaceline(char *buf, int len, char *s)
{
  int i;

  for(i = 0; i < len; i++)
    write(2, "\b \b", 3);
  strcpy(buf, s);
  len = strlen(buf);
  write(2, buf, len);
  return len;
}

static int
readline(char *buf, int nbuf)
{
  char saved[MAXLINE];
  char c, c1, c2;
  int len = 0, idx = hcount, first, r, pending, quiet;

  memset(buf, 0, nbuf);
  saved[0] = 0;
  pending = consraw(1);
  write(2, "$ ", 2);

  for(;;){
    r = read(0, &c, 1);
    if(r < 0 || (r == 0 && len == 0)){
      consraw(0);
      return -1;
    }
    if(r == 0)
      continue;

    quiet = pending > 0;
    if(quiet)
      pending--;

    if(c == '\n' || c == '\r'){
      if(!quiet)
        write(2, "\n", 1);
      break;
    } else if(quiet){
      if(c >= ' ' && c < 127 && len < nbuf - 1)
        buf[len++] = c;
    } else if(c == 127 || c == '\b'){
      if(len > 0){
        buf[--len] = 0;
        write(2, "\b \b", 3);
      }
    } else if(c == 21){
      len = replaceline(buf, len, "");
    } else if(c == '\t'){
      len = complete(buf, len);
    } else if(c == 27){
      if(read(0, &c1, 1) != 1 || read(0, &c2, 1) != 1)
        continue;
      if(c1 != '[' && c1 != 'O')
        continue;
      first = hcount > MAXHIST ? hcount - MAXHIST : 0;
      if(c2 == 'A' && idx > first){
        if(idx == hcount)
          strcpy(saved, buf);
        idx--;
        len = replaceline(buf, len, hist[idx % MAXHIST]);
      } else if(c2 == 'B' && idx < hcount){
        idx++;
        len = replaceline(buf, len, idx == hcount ? saved : hist[idx % MAXHIST]);
      }
    } else if(c >= ' ' && c < 127){
      if(len < nbuf - 1){
        buf[len++] = c;
        write(2, &c, 1);
      }
    }
  }

  consraw(0);
  buf[len] = 0;
  return 0;
}

struct cmd {
  int type;
};

struct execcmd {
  int type;
  char *argv[MAXARGS];
  char *eargv[MAXARGS];
};

struct redircmd {
  int type;
  struct cmd *cmd;
  char *file;
  char *efile;
  int mode;
  int fd;
};

struct pipecmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct listcmd {
  int type;
  struct cmd *left;
  struct cmd *right;
};

struct backcmd {
  int type;
  struct cmd *cmd;
};

int fork1(void); // Fork but panics on failure.
void panic(char *);
struct cmd *parsecmd(char *);
void runcmd(struct cmd *) __attribute__((noreturn));

// Execute cmd.  Never returns.
void
runcmd(struct cmd *cmd)
{
  int p[2];
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    exit(1);

  switch (cmd->type) {
  default:
    panic("runcmd");

  case EXEC:
    ecmd = (struct execcmd *)cmd;
    if (ecmd->argv[0] == 0)
      exit(1);
    if (strcmp(ecmd->argv[0], "history") == 0) {
      printhist();
      exit(0);
    }
    exec(ecmd->argv[0], ecmd->argv);
    fprintf(2, "exec %s failed\n", ecmd->argv[0]);
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    close(rcmd->fd);
    if (open(rcmd->file, rcmd->mode) < 0) {
      fprintf(2, "open %s failed\n", rcmd->file);
      exit(1);
    }
    runcmd(rcmd->cmd);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    if (fork1() == 0)
      runcmd(lcmd->left);
    wait(0);
    runcmd(lcmd->right);
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    if (pipe(p) < 0)
      panic("pipe");
    if (fork1() == 0) {
      close(1);
      dup(p[1]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->left);
    }
    if (fork1() == 0) {
      close(0);
      dup(p[0]);
      close(p[0]);
      close(p[1]);
      runcmd(pcmd->right);
    }
    close(p[0]);
    close(p[1]);
    wait(0);
    wait(0);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    if (fork1() == 0)
      runcmd(bcmd->cmd);
    break;
  }
  exit(0);
}

int
getcmd(char *buf, int nbuf)
{
  int n;

  if(interactive)
    return readline(buf, nbuf);

  memset(buf, 0, nbuf);
  gets(buf, nbuf);
  if(buf[0] == 0)
    return -1;
  n = strlen(buf);
  if(buf[n-1] == '\n')
    buf[n-1] = 0;
  return 0;
}

int
main(void)
{
  static char buf[MAXLINE];
  struct stat st;
  int fd, r;

  // Ensure that three file descriptors are open.
  while((fd = open("console", O_RDWR)) >= 0){
    if(fd >= 3){
      close(fd);
      break;
    }
  }

  interactive = (fstat(0, &st) == 0 && st.type == T_DEVICE);

  // Read and run input commands.
  while(getcmd(buf, sizeof(buf)) >= 0){
    if(buf[0] == 0)
      continue;

    if(buf[0] == '!'){ 
      r = expand_history(buf);
      if(r < 0)
        continue;
      if(r > 0)
        printf("%s\n", buf);
    }
    addhistory(buf);

    if(buf[0] == 'c' && buf[1] == 'd' && buf[2] == ' '){
      if(chdir(buf+3) < 0)
        fprintf(2, "cannot chdir %s\n", buf+3);
      continue;
    }
    if(fork1() == 0)
      runcmd(parsecmd(buf));
    wait(0);
  }
  exit(0);
}

void
panic(char *s)
{
  fprintf(2, "%s\n", s);
  exit(1);
}

int
fork1(void)
{
  int pid;

  pid = fork();
  if (pid == -1)
    panic("fork");
  return pid;
}

//PAGEBREAK!
// Constructors

struct cmd *
execcmd(void)
{
  struct execcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = EXEC;
  return (struct cmd *)cmd;
}

struct cmd *
redircmd(struct cmd *subcmd, char *file, char *efile, int mode, int fd)
{
  struct redircmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = REDIR;
  cmd->cmd = subcmd;
  cmd->file = file;
  cmd->efile = efile;
  cmd->mode = mode;
  cmd->fd = fd;
  return (struct cmd *)cmd;
}

struct cmd *
pipecmd(struct cmd *left, struct cmd *right)
{
  struct pipecmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = PIPE;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
listcmd(struct cmd *left, struct cmd *right)
{
  struct listcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = LIST;
  cmd->left = left;
  cmd->right = right;
  return (struct cmd *)cmd;
}

struct cmd *
backcmd(struct cmd *subcmd)
{
  struct backcmd *cmd;

  cmd = malloc(sizeof(*cmd));
  memset(cmd, 0, sizeof(*cmd));
  cmd->type = BACK;
  cmd->cmd = subcmd;
  return (struct cmd *)cmd;
}
//PAGEBREAK!
// Parsing

char whitespace[] = " \t\r\n\v";
char symbols[] = "<|>&;()";

int
gettoken(char **ps, char *es, char **q, char **eq)
{
  char *s;
  int ret;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  if (q)
    *q = s;
  ret = *s;
  switch (*s) {
  case 0:
    break;
  case '|':
  case '(':
  case ')':
  case ';':
  case '&':
  case '<':
    s++;
    break;
  case '>':
    s++;
    if (*s == '>') {
      ret = '+';
      s++;
    }
    break;
  default:
    ret = 'a';
    while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
      s++;
    break;
  }
  if (eq)
    *eq = s;

  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return ret;
}

int
peek(char **ps, char *es, char *toks)
{
  char *s;

  s = *ps;
  while (s < es && strchr(whitespace, *s))
    s++;
  *ps = s;
  return *s && strchr(toks, *s);
}

struct cmd *parseline(char **, char *);
struct cmd *parsepipe(char **, char *);
struct cmd *parseexec(char **, char *);
struct cmd *nulterminate(struct cmd *);

struct cmd *
parsecmd(char *s)
{
  char *es;
  struct cmd *cmd;

  es = s + strlen(s);
  cmd = parseline(&s, es);
  peek(&s, es, "");
  if (s != es) {
    fprintf(2, "leftovers: %s\n", s);
    panic("syntax");
  }
  nulterminate(cmd);
  return cmd;
}

struct cmd *
parseline(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parsepipe(ps, es);
  while (peek(ps, es, "&")) {
    gettoken(ps, es, 0, 0);
    cmd = backcmd(cmd);
  }
  if (peek(ps, es, ";")) {
    gettoken(ps, es, 0, 0);
    cmd = listcmd(cmd, parseline(ps, es));
  }
  return cmd;
}

struct cmd *
parsepipe(char **ps, char *es)
{
  struct cmd *cmd;

  cmd = parseexec(ps, es);
  if (peek(ps, es, "|")) {
    gettoken(ps, es, 0, 0);
    cmd = pipecmd(cmd, parsepipe(ps, es));
  }
  return cmd;
}

struct cmd *
parseredirs(struct cmd *cmd, char **ps, char *es)
{
  int tok;
  char *q, *eq;

  while (peek(ps, es, "<>")) {
    tok = gettoken(ps, es, 0, 0);
    if (gettoken(ps, es, &q, &eq) != 'a')
      panic("missing file for redirection");
    switch (tok) {
    case '<':
      cmd = redircmd(cmd, q, eq, O_RDONLY, 0);
      break;
    case '>':
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE | O_TRUNC, 1);
      break;
    case '+': // >>
      cmd = redircmd(cmd, q, eq, O_WRONLY | O_CREATE, 1);
      break;
    }
  }
  return cmd;
}

struct cmd *
parseblock(char **ps, char *es)
{
  struct cmd *cmd;

  if (!peek(ps, es, "("))
    panic("parseblock");
  gettoken(ps, es, 0, 0);
  cmd = parseline(ps, es);
  if (!peek(ps, es, ")"))
    panic("syntax - missing )");
  gettoken(ps, es, 0, 0);
  cmd = parseredirs(cmd, ps, es);
  return cmd;
}

struct cmd *
parseexec(char **ps, char *es)
{
  char *q, *eq;
  int tok, argc;
  struct execcmd *cmd;
  struct cmd *ret;

  if (peek(ps, es, "("))
    return parseblock(ps, es);

  ret = execcmd();
  cmd = (struct execcmd *)ret;

  argc = 0;
  ret = parseredirs(ret, ps, es);
  while (!peek(ps, es, "|)&;")) {
    if ((tok = gettoken(ps, es, &q, &eq)) == 0)
      break;
    if (tok != 'a')
      panic("syntax");
    cmd->argv[argc] = q;
    cmd->eargv[argc] = eq;
    argc++;
    if (argc >= MAXARGS)
      panic("too many args");
    ret = parseredirs(ret, ps, es);
  }
  cmd->argv[argc] = 0;
  cmd->eargv[argc] = 0;
  return ret;
}

// NUL-terminate all the counted strings.
struct cmd *
nulterminate(struct cmd *cmd)
{
  int i;
  struct backcmd *bcmd;
  struct execcmd *ecmd;
  struct listcmd *lcmd;
  struct pipecmd *pcmd;
  struct redircmd *rcmd;

  if (cmd == 0)
    return 0;

  switch (cmd->type) {
  case EXEC:
    ecmd = (struct execcmd *)cmd;
    for (i = 0; ecmd->argv[i]; i++)
      *ecmd->eargv[i] = 0;
    break;

  case REDIR:
    rcmd = (struct redircmd *)cmd;
    nulterminate(rcmd->cmd);
    *rcmd->efile = 0;
    break;

  case PIPE:
    pcmd = (struct pipecmd *)cmd;
    nulterminate(pcmd->left);
    nulterminate(pcmd->right);
    break;

  case LIST:
    lcmd = (struct listcmd *)cmd;
    nulterminate(lcmd->left);
    nulterminate(lcmd->right);
    break;

  case BACK:
    bcmd = (struct backcmd *)cmd;
    nulterminate(bcmd->cmd);
    break;
  }
  return cmd;
}
