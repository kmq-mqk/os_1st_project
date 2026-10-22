#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/param.h"

static char *target;
static char **cmd;
static int ncmd;

static char*
basename(char *path)
{
  char *p;

  for(p = path + strlen(path); p >= path && *p != '/'; p--)
    ;
  return p + 1;
}

static void
action(char *path)
{
  char *argv[MAXARG];
  int i, pid;

  if(ncmd == 0){
    printf("%s\n", path);
    return;
  }

  for(i = 0; i < ncmd; i++)
    argv[i] = cmd[i];
  argv[ncmd] = path;
  argv[ncmd + 1] = 0;

  pid = fork();
  if(pid < 0){
    fprintf(2, "find: fork failed\n");
    return;
  }
  if(pid == 0){
    exec(argv[0], argv);
    fprintf(2, "find: exec %s failed\n", argv[0]);
    exit(1);
  }
  wait(0);
}

static void
find(char *path)
{
  char buf[512], *p;
  int fd;
  struct dirent de;
  struct stat st;

  if((fd = open(path, O_RDONLY)) < 0){
    fprintf(2, "find: cannot open %s\n", path);
    return;
  }
  if(fstat(fd, &st) < 0){
    fprintf(2, "find: cannot stat %s\n", path);
    close(fd);
    return;
  }

  if(st.type == T_DIR){
    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)){
      fprintf(2, "find: path too long\n");
      close(fd);
      return;
    }
    strcpy(buf, path);
    p = buf + strlen(buf);
    if(p > buf && p[-1] != '/')
      *p++ = '/';

    while(read(fd, &de, sizeof(de)) == sizeof(de)){
      if(de.inum == 0)
        continue;
      memmove(p, de.name, DIRSIZ);
      p[DIRSIZ] = 0;

      if(strcmp(p, ".") == 0 || strcmp(p, "..") == 0)
        continue;
      if(strcmp(p, target) == 0)
        action(buf);
      find(buf);
    }
  }
  close(fd);
}

int
main(int argc, char *argv[])
{
  if(argc < 3){
    fprintf(2, "usage: find dir name [-exec cmd [args...]]\n");
    exit(1);
  }
  target = argv[2];

  if(argc > 3){
    if(strcmp(argv[3], "-exec") != 0 || argc < 5){
      fprintf(2, "usage: find dir name [-exec cmd [args...]]\n");
      exit(1);
    }
    cmd = &argv[4];
    ncmd = argc - 4;
    if(ncmd > MAXARG - 2){
      fprintf(2, "find: -exec command too long\n");
      exit(1);
    }
  }

  if(strcmp(basename(argv[1]), target) == 0)
    action(argv[1]);
  find(argv[1]);
  exit(0);
}