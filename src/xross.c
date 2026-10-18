/* XrossOS shell — C/Xlib. Классический оконный интерфейс в духе
 * Mac OS 7–9 (Platinum) и Haiku: строка меню сверху, рабочий стол с
 * иконками, окна с полосатой полосой заголовка, кнопками закрытия и
 * изменения размера, серые скошенные панели.
 *
 * xross одновременно:
 *  1) рабочий стол (override-redirect окно на весь экран);
 *  2) минимальный оконный менеджер: SubstructureRedirect на root —
 *     каждое внешнее приложение получает рамку окна (заголовок,
 *     закрытие, разворачивание, перетаскивание), а не весь экран;
 *  3) клиент голосового демона xross-voiced (команды из FIFO).
 *
 * Рендер: программный, двойная буферизация Pixmap, только ядро X11
 * (без XRender/композитора — важно для Radeon HD 8330 + Jaguar). */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <ctype.h>
#include <locale.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "xross.h"

/* ───────────────────────── типы и состояние ───────────────────────── */
typedef struct { int x, y, w, h, action, arg; } Hit;
typedef struct { const char *cat, *title, *sub, *glyph; unsigned rgb; int ext; const char *cmd; } AppDef;

enum { MODE_BOOT, MODE_DESKTOP, MODE_APP };
enum { APP_NONE = -1 };
/* action-коды хитбоксов */
enum { ACT_ITEM, ACT_SETTING, ACT_TRACK, ACT_STORE, ACT_CLOSEWIN, ACT_MENU, ACT_MENUITEM, ACT_MINWIN, ACT_DESK };

static Display *dpy; static Window root_win, shell_win; static Pixmap backbuf;
static GC gc; static XFontSet fontset; static XFontStruct *fixedfont;
static int W, H, screen_no;
static Colormap cmap;

static int mode = MODE_BOOT;
static int item_idx = 0;
static double anim_item = 0.0; /* сглаживание выбора иконки */
static double boot_t0;
static int open_app = APP_NONE;                /* индекс в APPS или -1 */
static Window client_win = None;               /* чужое полноэкранное окно */
static pid_t child_pid = -1;
static char toast_msg[256]; static double toast_until;
static Hit hits[128]; static int nhits;
static long frame_no;

/* настройки (сохраняются в CONFIG_PATH) */
static int cfg_desk = 0;         /* 0=бирюзовый (Mac), 1=серый, 2=синий (Haiku) */
static int cfg_anim = 1, cfg_fps_battery_save = 1;
static const unsigned DESK_RGB[3] = { 0x386e6e, 0x5a6e7a, 0x2c4a72 };
static const unsigned ACCENT_RGB = 0xffc914; /* жёлтый таб Haiku */

static const AppDef APPS[] = {
    {"settings","Настройки системы","Внешний вид, анимации, FPS","S",0x9aa7b4,0,NULL},
    {"settings","Голосовое управление","Vosk офлайн · клавиша V","V",0xd94f6a,0,NULL},
    {"settings","О системе","XrossOS " XROSS_VERSION " · ASUS X550EA","i",0x4a6fa5,0,NULL},
    {"photo","Фотографии","Галерея","P",0x7a9e43,0,NULL},
    {"music","Музыка","Треки и эквалайзер","M",0xb05a7a,0,NULL},
    {"video","Видео (mpv)","Внешний плеер в окне","▶",0xc96a2c,1,"mpv --idle"},
    {"network","Браузер Firefox","Внешнее приложение в окне","F",0xd2691e,1,"firefox-esr"},
    {"network","Магазин","Установка пакетов apk","+",0xb8860b,0,NULL},
    {"apps","Файлы","Домашний каталог","D",0xc9a227,0,NULL},
    {"apps","Терминал (xterm)","Внешнее приложение в окне",">",0x444444,1,"xterm -fa DejaVuSansMono -fs 13 -bg white -fg black"},
    {"apps","Заметки","Сохраняются в ~/.xross-notes","N",0x8a7a2c,0,NULL},
};
#define NAPPS (int)(sizeof APPS / sizeof APPS[0])

/* ───────────────────────── утилиты ───────────────────────── */
static double now_sec(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec + ts.tv_nsec/1e9; }
static void toast_set(const char *s){ snprintf(toast_msg,sizeof toast_msg,"%s",s); toast_until = now_sec()+2.6; }

static unsigned long px(unsigned rgb){
    /* кэш на 64 цвета: XAllocColor на каждый кадр убьёт Jaguar */
    static struct { unsigned rgb; unsigned long p; } cache[128]; static int n;
    for (int i=0;i<n;i++) if (cache[i].rgb==rgb) return cache[i].p;
    XColor c; c.red=((rgb>>16)&255)*257; c.green=((rgb>>8)&255)*257; c.blue=(rgb&255)*257; c.flags=DoRed|DoGreen|DoBlue;
    if (!XAllocColor(dpy,cmap,&c)) c.pixel = (rgb&0xffffff);
    if (n<128){ cache[n].rgb=rgb; cache[n].p=c.pixel; n++; }
    return c.pixel;
}
static unsigned blend(unsigned a, unsigned b, double t){
    int r=(int)(((a>>16)&255)*(1-t)+((b>>16)&255)*t), g=(int)(((a>>8)&255)*(1-t)+((b>>8)&255)*t), bl=(int)((a&255)*(1-t)+(b&255)*t);
    return (r<<16)|(g<<8)|bl;
}
static void fg(unsigned rgb){ XSetForeground(dpy,gc,px(rgb)); }
static void fill_rect(int x,int y,int w,int h,unsigned rgb){ fg(rgb); XFillRectangle(dpy,backbuf,gc,x,y,w,h); }
static void fill_circle(int cx,int cy,int r,unsigned rgb){ fg(rgb); XFillArc(dpy,backbuf,gc,cx-r,cy-r,r*2,r*2,0,360*64); }
static void draw_circle(int cx,int cy,int r,unsigned rgb,int lw){ fg(rgb); XSetLineAttributes(dpy,gc,lw,LineSolid,CapRound,JoinRound); XDrawArc(dpy,backbuf,gc,cx-r,cy-r,r*2,r*2,0,360*64); }

/* текст: XFontSet + UTF-8 (русский без Xft/FreeType в рантайме оболочки) */
static void text(const char *s,int x,int y,unsigned rgb){
    fg(rgb);
    if (fontset) Xutf8DrawString(dpy,backbuf,fontset,gc,x,y,s,strlen(s));
    else if (fixedfont){ XSetFont(dpy,gc,fixedfont->fid); XDrawString(dpy,backbuf,gc,x,y,s,strlen(s)); }
}
static int text_w(const char *s){
    if (fontset){ XRectangle ink,logi; Xutf8TextExtents(fontset,s,strlen(s),&ink,&logi); return logi.width; }
    return (int)strlen(s)*8;
}
static void text_c(const char *s,int cx,int y,unsigned rgb){ text(s,cx-text_w(s)/2,y,rgb); }
static void add_hit(int x,int y,int w,int h,int action,int arg){ if(nhits<128) hits[nhits++]=(Hit){x,y,w,h,action,arg}; }

static Cursor arrow_cursor;
static Cursor make_arrow_cursor(void){
    /* классическая стрелка Mac: чёрное тело, белый контур; hot spot (1,1) */
    static const char *src[16]={
        "X...............","XX..............","XXX.............","XXXX............",
        "XXXXX...........","XXXXXX..........","XXXXXXX.........","XXXXXXXX........",
        "XXXX.XXX........","XX...XXX........","X.....XXX.......","......XXX.......",
        ".......XX.......","........X.......","................","................"};
    static const char *msk[16]={
        "XX..............","XXX.............","XXXX............","XXXXX...........",
        "XXXXXX..........","XXXXXXX.........","XXXXXXXX........","XXXXXXXXX.......",
        "XXXX.XXXX.......","XX...XXXX.......","X.....XXXX......","......XXXX......",
        ".......XXX......","........XX......","................","................"};
    Pixmap sp=XCreatePixmap(dpy,root_win,16,16,1), mp=XCreatePixmap(dpy,root_win,16,16,1);
    if(!sp || !mp) return None;
    GC g=XCreateGC(dpy,sp,0,NULL);
    for(int pass=0;pass<2;pass++){
        Drawable dd=pass? mp:sp; const char **rows=pass? msk:src;
        XSetForeground(dpy,g,0); XFillRectangle(dpy,dd,g,0,0,16,16);
        XSetForeground(dpy,g,1);
        for(int y=0;y<16;y++) for(int x=0;x<16;x++) if(rows[y][x]=='X') XDrawPoint(dpy,dd,g,x,y);
    }
    XFreeGC(dpy,g);
    XColor blk={0,0,0,0,DoRed|DoGreen|DoBlue}, wht={0,65535,65535,65535,DoRed|DoGreen|DoBlue};
    Cursor c=XCreatePixmapCursor(dpy,sp,mp,&blk,&wht,1,1);
    XFreePixmap(dpy,sp); XFreePixmap(dpy,mp);
    return c;
}

/* ── Platinum (Mac OS 7–9) + Haiku примитивы ───────────────────────────
 * Скошенная панель: светлая кромка сверху-слева, тёмная снизу-справа. */
static void bevel(int x,int y,int w,int h,int raised){
    unsigned hi=raised?0xffffff:0x555555, lo=raised?0x555555:0xffffff, mid=raised?0x999999:0xdddddd;
    fg(hi);  XDrawLine(dpy,backbuf,gc,x,y,x+w-1,y); XDrawLine(dpy,backbuf,gc,x,y,x,y+h-1);
    fg(mid); XDrawLine(dpy,backbuf,gc,x+1,y+1,x+w-2,y+1); XDrawLine(dpy,backbuf,gc,x+1,y+1,x+1,y+h-2);
    fg(lo);  XDrawLine(dpy,backbuf,gc,x,y+h-1,x+w-1,y+h-1); XDrawLine(dpy,backbuf,gc,x+w-1,y,x+w-1,y+h-1);
    fg(raised?0x777777:0xbbbbbb); XDrawLine(dpy,backbuf,gc,x+1,y+h-2,x+w-2,y+h-2); XDrawLine(dpy,backbuf,gc,x+w-2,y+1,x+w-2,y+h-2);
}
static void panel(int x,int y,int w,int h){ fill_rect(x,y,w,h,0xdddddd); bevel(x,y,w,h,1); }
/* Полоса заголовка окна: серые горизонтальные линии, как в Platinum,
 * с жёлтой вкладкой-вкладкой в духе Haiku слева от заголовка. */
static void titlebar(int x,int y,int w,const char *title,int active){
    fill_rect(x,y,w,22, active?0xdddddd:0xbbbbbb);
    if (active){ fg(0x999999); for(int yy=y+4;yy<y+20;yy+=3) XDrawLine(dpy,backbuf,gc,x+30,y+ (yy-y),x+w-30,yy); }
    fill_rect(x+4,y+3,10,16,ACCENT_RGB); bevel(x+4,y+3,10,16,1);
    /* кнопка закрытия (Mac: слева) */
    fill_rect(x+18,y+5,12,12,0xdddddd); bevel(x+18,y+5,12,12,1);
    /* кнопка разворачивания (справа) */
    fill_rect(x+w-18,y+5,12,12,0xdddddd); bevel(x+w-18,y+5,12,12,1);
    fg(0x000000); { XPoint sq[5]={{x+w-15,y+8},{x+w-9,y+8},{x+w-9,y+14},{x+w-15,y+14},{x+w-15,y+8}}; XDrawLines(dpy,backbuf,gc,sq,5,CoordModeOrigin); }
    if (title) text_c(title,x+w/2,y+16,0x000000);
    fg(0x777777); XDrawLine(dpy,backbuf,gc,x,y+22,x+w,y+22);
}
/* Классическая иконка рабочего стола: папка / документ / программа */
static void desk_icon(int cx,int cy,const AppDef *a,int focused){
    int x=cx-16, y=cy-16;
    if (!strcmp(a->title,"Файлы")){ /* жёлтая папка */
        fill_rect(x+2,y+4,12,6,0xe8c84a); bevel(x+2,y+4,12,6,1);
        fill_rect(x,y+9,32,20,0xf2d060); bevel(x,y+9,32,20,1);
        fg(0xb8860b); XDrawLine(dpy,backbuf,gc,x+4,y+16,x+28,y+16);
    } else if (!strcmp(a->cat,"settings")){ /* шестерёнка-«гайка»: квадрат с отверстием */
        fill_rect(x+4,y+4,24,24,a->rgb); bevel(x+4,y+4,24,24,1);
        fill_circle(cx,cy,6,0xdddddd); fill_circle(cx,cy,3,0x777777);
    } else { /* документ/программа: лист с цветной полосой и глифом */
        fill_rect(x+5,y+2,22,28,0xffffff); bevel(x+5,y+2,22,28,1);
        fill_rect(x+5,y+2,22,7,a->rgb);
        fg(0x888888); for(int i=0;i<3;i++) XDrawLine(dpy,backbuf,gc,x+9,y+15+i*4,x+23,y+15+i*4);
        if (a->glyph) text_c(a->glyph,cx,y+8,0xffffff);
    }
    if (focused){ fg(0xffffff); XDrawRectangle(dpy,backbuf,gc,cx-22,cy-22,44,44); fg(0x000000); XDrawRectangle(dpy,backbuf,gc,cx-23,cy-23,46,46); }
}

/* батарея/питание X550EA: BAT0 + ADP1 (имена стабильны на этой модели) */
static int read_int_file(const char *p){ FILE *f=fopen(p,"r"); if(!f) return -1; int v=-1; if(fscanf(f,"%d",&v)!=1) v=-1; fclose(f); return v; }
static int battery_pct(void){ int v=read_int_file("/sys/class/power_supply/BAT0/capacity"); if(v<0) v=read_int_file("/sys/class/power_supply/BAT1/capacity"); return v; }
static int on_ac(void){ int v=read_int_file("/sys/class/power_supply/ADP1/online"); if(v<0) v=read_int_file("/sys/class/power_supply/AC/online"); return v==1; }
static int target_fps(void){ return (cfg_fps_battery_save && !on_ac()) ? FPS_BATTERY : FPS_AC; }

/* ───────────────────────── конфиг ───────────────────────── */
static void path_home(char *out,size_t n,const char *rel){ snprintf(out,n,"%s%s",getenv("HOME")?getenv("HOME"):"/root",rel); }
static void config_load(void){
    char p[512]; path_home(p,sizeof p,CONFIG_PATH); FILE *f=fopen(p,"r"); if(!f) return; char k[64],v[64];
    while (fscanf(f,"%63[^=]=%63s\n",k,v)==2){ if(!strcmp(k,"desk"))cfg_desk=atoi(v)%3;
        else if(!strcmp(k,"anim"))cfg_anim=atoi(v); else if(!strcmp(k,"fps_battery_save"))cfg_fps_battery_save=atoi(v); }
    fclose(f);
}
static void config_save(void){
    char p[512]; path_home(p,sizeof p,CONFIG_PATH); char d[512]; snprintf(d,sizeof d,"%s",p); char *s=strrchr(d,'/'); if(s){*s=0; mkdir(d,0755);} 
    /* ~/.config может не быть: создать поэтапно */
    char mk[600]; path_home(mk,sizeof mk,"/.config"); mkdir(mk,0755); path_home(mk,sizeof mk,"/.config/xross"); mkdir(mk,0755);
    FILE *f=fopen(p,"w"); if(!f) return;
    fprintf(f,"desk=%d\nanim=%d\nfps_battery_save=%d\n",cfg_desk,cfg_anim,cfg_fps_battery_save); fclose(f);
}

/* ───────────────────────── данные приложений ───────────────────────── */
static char notes_buf[4096]; static int music_track=-1; static double music_t0;
static int store_progress[4];

static void notes_load(void){ char p[512]; path_home(p,sizeof p,NOTES_PATH); FILE *f=fopen(p,"r"); if(f){ size_t n=fread(notes_buf,1,sizeof notes_buf-1,f); notes_buf[n]=0; fclose(f);} }
static void notes_save(void){ char p[512]; path_home(p,sizeof p,NOTES_PATH); FILE *f=fopen(p,"w"); if(f){ fputs(notes_buf,f); fclose(f);} }

/* ───────────────────────── запуск внешних приложений ───────────────────────── */
/* есть ли бинарь в PATH (первое слово команды) */
static int have_binary(const char *cmdline){
    char prog[128]; size_t k=0; while (cmdline[k] && cmdline[k]!=' ' && k<sizeof prog-1){ prog[k]=cmdline[k]; k++; } prog[k]=0;
    if (strchr(prog,'/')) return access(prog,X_OK)==0;
    const char *path=getenv("PATH"); if(!path) path="/usr/bin:/bin:/usr/local/bin";
    char dup[512]; snprintf(dup,sizeof dup,"%s",path); char *save=NULL;
    for (char *d=strtok_r(dup,":",&save); d; d=strtok_r(NULL,":",&save)){
        char full[640]; snprintf(full,sizeof full,"%s/%s",d,prog);
        if (access(full,X_OK)==0) return 1;
    }
    return 0;
}
static void launch_external(const char *cmdline, const char *title){
    if (!have_binary(cmdline)){
        char b[256]; snprintf(b,sizeof b,"%s не установлено в Live-образе",title); toast_set(b);
        fprintf(stderr,"xross: not installed: %s (%s)\n",title,cmdline);
        return;
    }
    toast_set("Запуск приложения в окне…");
    pid_t pid=fork();
    if (pid==0){ setsid();
        /* через sh -c: раскрываются ~, переменные и кавычки в командах реестра */
        execl("/bin/sh","sh","-c",cmdline,(char*)NULL); _exit(127); }
    child_pid=pid;
    XFlush(dpy);
    /* смерть ребёнка ловит главный цикл по kill(pid,0): при SIG_IGN waitpid не работает */
}
static void terminate_child(void){
    if (child_pid>0){
        /* приложение запущено через setsid: гасим всю группу процессов,
         * иначе за Firefox/xterm останутся висеть дочерние процессы */
        if (kill(-child_pid,SIGTERM)<0) kill(child_pid,SIGTERM);
        child_pid=-1;
    }
}
/* ── рамка окна внешнего приложения (оконный менеджер) ── */
static Window frame_win; static int fx=120, fy=60, fw=760, fh=520;
static int client_max, drag_on, drag_dx, drag_dy, save_x, save_y, save_w, save_h;
static int frame_hidden, internal_min;
#define TB 22 /* высота полосы заголовка */
static void draw_frame(void){
    if (!frame_win || client_win==None) return;
    GC fgc=XCreateGC(dpy,frame_win,0,NULL);
    XSetForeground(dpy,fgc,px(0xdddddd)); XFillRectangle(dpy,frame_win,fgc,0,0,fw,TB);
    XSetForeground(dpy,fgc,px(0x999999));
    for(int yy=4;yy<TB-3;yy+=3) XDrawLine(dpy,frame_win,fgc,34,yy,fw-30,yy);
    XSetForeground(dpy,fgc,px(ACCENT_RGB)); XFillRectangle(dpy,frame_win,fgc,4,3,10,16);
    /* кнопка закрытия слева, разворачивания справа */
    XSetForeground(dpy,fgc,px(0xdddddd)); XFillRectangle(dpy,frame_win,fgc,18,5,12,12);
    XSetForeground(dpy,fgc,px(0x000000)); XDrawRectangle(dpy,frame_win,fgc,18,5,12,12);
    XDrawRectangle(dpy,frame_win,fgc,fw-18,5,12,12); XDrawRectangle(dpy,frame_win,fgc,fw-15,8,6,6);
    XDrawRectangle(dpy,frame_win,fgc,fw-36,5,12,12); XDrawLine(dpy,frame_win,fgc,fw-33,14,fw-27,14);
    XSetForeground(dpy,fgc,px(0x000000));
    const char *t="Приложение";
    if (fontset) Xutf8DrawString(dpy,frame_win,fontset,fgc,fw/2-40,16,t,strlen(t));
    XFreeGC(dpy,fgc); XFlush(dpy);
}
static void frame_apply(void){
    if (!frame_win) return;
    XMoveResizeWindow(dpy,frame_win,fx,fy,fw,fh);
    if (client_win!=None) XMoveResizeWindow(dpy,client_win,0,TB,fw>40?fw-2:fw,fh>TB+2?fh-TB-2:fh-TB);
    draw_frame();
}
static void close_client(void){
    if (client_win!=None){ XKillClient(dpy,client_win); client_win=None; }
    terminate_child();
    if (frame_win){ XDestroyWindow(dpy,frame_win); frame_win=None; }
    client_max=0; drag_on=0;
    XMapRaised(dpy,shell_win); XSetInputFocus(dpy,shell_win,RevertToParent,CurrentTime);
    mode=MODE_DESKTOP; open_app=APP_NONE;
    toast_set("Приложение закрыто");
}

static void open_app_idx(int ai){
    if (ai<0) return;
    if (APPS[ai].ext){ launch_external(APPS[ai].cmd, APPS[ai].title); return; }
    open_app=ai; mode=MODE_APP; internal_min=0;
    char b[128]; snprintf(b,sizeof b,"%s · Esc — закрыть окно",APPS[ai].title); toast_set(b);
}

/* ───────────────────────── голос ───────────────────────── */
static int voice_fd=-1; static char voice_buf[512]; static int voice_len;
static void speak(const char *s){ /* TTS если есть espeak-ng: не блокируем UI */
    if (fork()==0){ execlp("espeak-ng","espeak-ng","-v","ru",s,(char*)NULL); _exit(0);} }
static void voice_feedback(const char *s){ toast_set(s); speak(s); }
static void do_move(int dc,int di); static void do_select(void); static void do_back(void);
static void voice_handle(char *cmd){
    for (char *p=cmd; *p; p++) { /* пробелы/переносы в разделители, регистр выставляет демон */ if(*p=='\n'||*p=='\r') *p=0; }
    if (!*cmd) return;
    char t[320]; snprintf(t,sizeof t,"Голос: %s",cmd); toast_set(t);
    if (strstr(cmd,"помощь")||strstr(cmd,"команды")) return voice_feedback("Команды: вправо, вниз, выбери, открой музыку, домой, который час");
    if (strstr(cmd,"который час")||strstr(cmd,"время")){ time_t tt=time(NULL); char b[64]; strftime(b,sizeof b,"Сейчас %H:%M",localtime(&tt)); return voice_feedback(b); }
    if (strstr(cmd,"домой")||strstr(cmd,"главн")||strstr(cmd,"закрой")||strstr(cmd,"выйди")){ do_back(); return voice_feedback("Главный экран"); }
    if (strstr(cmd,"вправо")) return do_move(1,0);
    if (strstr(cmd,"влево")) return do_move(-1,0);
    if (strstr(cmd,"вниз")) return do_move(0,1);
    if (strstr(cmd,"вверх")) return do_move(0,-1);
    const char *o=strstr(cmd,"открой"); if(!o) o=strstr(cmd,"запусти");
    if (o){ o+=strlen("открой"); while(*o==' ')o++; char needle[128]; snprintf(needle,sizeof needle,"%s",o);
        /* ищем по первому слову названия */
        for(int i=0;i<NAPPS;i++){ char low[128]; snprintf(low,sizeof low,"%s",APPS[i].title);
            /* грубое сравнение: название приложения содержится/подстрока на русском в нижнем регистре даёт демон */
            if (strstr(low,needle)||strstr(needle,low)){ open_app_idx(i); return voice_feedback(APPS[i].title); } }
        /* fallback: популярные синонимы */
        struct { const char *w; const char *title; } syn[]={{"музык","Музыка"},{"настрой","Настройки системы"},{"файл","Файлы"},{"термин","Терминал (xterm)"},{"брауз","Браузер Firefox"},{"замет","Заметки"},{"фото","Фотографии"}};
        for(unsigned i=0;i<sizeof syn/sizeof syn[0];i++) if(strstr(cmd,syn[i].w)){ for(int j=0;j<NAPPS;j++) if(!strcmp(APPS[j].title,syn[i].title)){ open_app_idx(j); return; } }
        return voice_feedback("Приложение не найдено"); }
    if (strstr(cmd,"выбери")||strstr(cmd,"выбрать")||strstr(cmd,"ок")) return do_select();
    voice_feedback("Не поняла. Скажите: помощь");
}
static void voice_poll(void){
    if (voice_fd<0) return; char buf[256]; ssize_t n=read(voice_fd,buf,sizeof buf-1);
    if (n<=0) return; for(ssize_t i=0;i<n;i++){ if(buf[i]=='\n'||voice_len>=(int)sizeof voice_buf-2){ voice_buf[voice_len]=0; voice_handle(voice_buf); voice_len=0; } else voice_buf[voice_len++]=buf[i]; }
}

/* ───────────────────────── навигация ───────────────────────── */
#define DESK_COLS 2
static void do_move(int dc,int di){
    if (mode==MODE_APP) return;
    if (mode!=MODE_DESKTOP) { mode=MODE_DESKTOP; return; }
    /* иконки двумя колонками справа: ←→ между колонками, ↑↓ по колонке */
    if (dc) item_idx=(item_idx+dc+NAPPS)%NAPPS;
    if (di) item_idx=(item_idx+di*DESK_COLS+NAPPS)%NAPPS;
}
static void do_select(void){
    if (mode==MODE_BOOT){ mode=MODE_DESKTOP; return; }
    if (mode==MODE_DESKTOP) open_app_idx(item_idx);
}
static void do_back(void){
    if (mode==MODE_APP){ open_app=APP_NONE; internal_min=0; mode=MODE_DESKTOP; if(music_track>=0) music_track=-1; notes_save(); return; }
    if (client_win!=None||child_pid>0) close_client();
}

/* ───────────────────────── фон рабочего стола ───────────────────────── */
static Pixmap bg_cache; static int bg_desk_cache=-1, bg_w, bg_h;
static void render_desktop_bg(void){
    if (bg_cache && bg_desk_cache==cfg_desk && bg_w==W && bg_h==H) { XCopyArea(dpy,bg_cache,backbuf,gc,0,0,W,H,0,0); return; }
    if (bg_cache) XFreePixmap(dpy,bg_cache);
    bg_cache=XCreatePixmap(dpy,root_win,W,H,DefaultDepth(dpy,screen_no)); bg_desk_cache=cfg_desk; bg_w=W; bg_h=H;
    GC g2=XCreateGC(dpy,bg_cache,0,NULL);
    unsigned base=DESK_RGB[cfg_desk];
    XSetForeground(dpy,g2,px(base)); XFillRectangle(dpy,bg_cache,g2,0,0,W,H);
    /* классический точечный паттерн рабочего стола */
    XSetForeground(dpy,g2,px(blend(base,0x000000,0.18)));
    for(int y=8;y<H;y+=16) for(int x=((y/16)%2?8:0);x<W;x+=16) XFillRectangle(dpy,bg_cache,g2,x,y,2,2);
    XFreeGC(dpy,g2); XCopyArea(dpy,bg_cache,backbuf,gc,0,0,W,H,0,0);
}

/* ───────────────────────── отрисовка экранов ───────────────────────── */
static int menu_open; /* 0=закрыто, 1=XrossOS, 2=Файл, 3=Вид, 4=Окно */
static void menu_item(const char *label,int x,int y,int w,int code){
    text(label,x+10,y+17,0x000000); add_hit(x,y,w,22,ACT_MENUITEM,code);
}
static void draw_topbar(void){
    fill_rect(0,0,W,24,0xdddddd);
    fg(0x777777); XDrawLine(dpy,backbuf,gc,0,24,W,24);
    text("◆ XrossOS",10,17,0x000000); add_hit(0,0,110,24,ACT_MENU,1);
    text("Файл",122,17,0x000000); add_hit(114,0,64,24,ACT_MENU,2);
    text("Правка",186,17,0x000000);
    text("Вид",262,17,0x000000); add_hit(254,0,52,24,ACT_MENU,3);
    text("Окно",310,17,0x000000); add_hit(302,0,60,24,ACT_MENU,4);
    if (menu_open) fill_rect(menu_open==1?2:(menu_open==2?114:(menu_open==3?254:302)),1,menu_open==1?108:66,22,0xbbbbbb);
    time_t tt=time(NULL); char b[96]; struct tm *tmv=localtime(&tt);
    int bat=battery_pct();
    if (bat>=0){ char bb[40]; snprintf(bb,sizeof bb,"Батарея %d%%",bat); text(bb,W-330,17,0x000000); }
    snprintf(b,sizeof b,"%02d:%02d",tmv->tm_hour,tmv->tm_min);
    text(b,W-64,17,0x000000);
    text("V — голос",W-250,17,0x444444);
    /* выпадающие меню */
    if (menu_open==1){ panel(2,26,250,76);
        menu_item("О системе",2,28,250,102); menu_item("Настройки системы",2,50,250,100); menu_item("Голосовое управление",2,72,250,101);
    } else if (menu_open==2){ panel(114,26,210,54);
        menu_item("Открыть выбранное",114,28,210,200); menu_item("Закрыть окно",114,50,210,201);
    } else if (menu_open==3){ panel(254,26,230,76);
        menu_item("Стол: бирюзовый",254,28,230,300); menu_item("Стол: серый",254,50,230,301); menu_item("Стол: синий",254,72,230,302);
    } else if (menu_open==4){ panel(302,26,280,76);
        menu_item("Рабочий стол",302,28,280,400);
        menu_item(open_app>=0?APPS[open_app].title:"(нет внутреннего окна)",302,50,280,401);
        menu_item(frame_win?"Внешнее приложение":"(нет внешнего окна)",302,72,280,402);
    }
}

/* нижняя панель окон в духе Haiku Deskbar: кнопки открытых окон */
static void draw_deskbar(void){
    if (mode==MODE_BOOT) return;
    fill_rect(0,H-28,W,28,0xdddddd); bevel(0,H-28,W,28,1);
    text("Окна:",8,H-10,0x000000);
    int x=64;
    if (open_app>=0){
        int w=text_w(APPS[open_app].title)+24;
        fill_rect(x,H-24,w,20,0xbbbbbb); bevel(x,H-24,w,20, mode==MODE_APP&&!internal_min?0:1);
        text(APPS[open_app].title,x+10,H-10,0x000000); add_hit(x,H-24,w,20,ACT_DESK,1); x+=w+8;
    }
    if (frame_win){
        const char *t="Внешнее приложение"; int w=text_w(t)+24;
        fill_rect(x,H-24,w,20,0xbbbbbb); bevel(x,H-24,w,20, frame_hidden?1:0);
        text(t,x+10,H-10,0x000000); add_hit(x,H-24,w,20,ACT_DESK,2); x+=w+8;
    }
    char b[256]; snprintf(b,sizeof b,"%s", mode==MODE_APP&&open_app>=0?APPS[open_app].sub:"Рабочий стол");
    text(b,x+8,H-10,0x444444);
}

static void draw_boot(double t){
    render_desktop_bg();
    int w=420,h=190,x=W/2-w/2,y=H/2-h/2;
    fill_rect(x+6,y+6,w,h,0x333333);
    panel(x,y,w,h); titlebar(x,y,w,"Добро пожаловать",1);
    text_c("XrossOS " XROSS_VERSION,w/2+x,y+78,0x000000);
    text_c("Классическая оконная среда",W/2,y+104,0x444444);
    int bw=w-80; fill_rect(x+40,y+128,bw,16,0xffffff); bevel(x+40,y+128,bw,16,0);
    double pr=t/2.2; if(pr>1)pr=1; fill_rect(x+42,y+130,(int)((bw-4)*pr),12,0x3366cc);
    text_c("Enter — пропустить",W/2,y+168,0x444444);
    if (t>2.2) mode=MODE_DESKTOP;
}

/* ── рабочий стол: иконки справа (Mac) + нижняя строка состояния ── */
#define DESK_COLS 2
static void draw_desktop(double t){
    render_desktop_bg();
    nhits=0;
    int cellw=124, cellh=88, x0=W-DESK_COLS*cellw-6, y0=36;
    for(int i=0;i<NAPPS;i++){
        int col=i%DESK_COLS, row=i/DESK_COLS;
        int cx=x0+col*cellw+cellw/2, cy=y0+row*cellh+28;
        int focused=(i==item_idx);
        desk_icon(cx,cy,&APPS[i],focused);
        /* подпись: в одну или две строки, у выбранной — чёрным по белому */
        const char *title=APPS[i].title;
        int ly=cy+38;
        if (focused){ int tw=text_w(title); fill_rect(cx-tw/2-4,ly-15,tw+8,20,0xffffff); }
        /* перенос по последнему пробелу, если не влезает */
        char l1[64]="",l2[64]="";
        if (text_w(title)>cellw-8){ const char *sp=strrchr(title,' ');
            if(sp){ snprintf(l1,sizeof l1,"%.*s",(int)(sp-title),title); snprintf(l2,sizeof l2,"%s",sp+1); }
            else snprintf(l1,sizeof l1,"%s",title);
        } else snprintf(l1,sizeof l1,"%s",title);
        text_c(l1,cx,ly,focused?0x000000:0xffffff);
        if(l2[0]) text_c(l2,cx,ly+18,focused?0x000000:0xffffff);
        add_hit(cx-cellw/2,cy-30,cellw,cellh-8,ACT_ITEM,i);
    }
}

static void card(int x,int y,int w,int h,const char *title){
    panel(x,y,w,h); if(title) text(title,x+16,y+26,0x000000);
}
static void draw_app(double t){
    draw_desktop(t);
    const AppDef *a=&APPS[open_app];
    fill_rect(20,42,W-34,H-62,0x333333);            /* тень окна */
    panel(14,36,W-28,H-68);                          /* окно */
    titlebar(14,36,W-28,a->title,1);
    add_hit(14+18,36+5,12,12,ACT_CLOSEWIN,0);
    add_hit(W-14-30,36+5,12,12,ACT_MINWIN,0);
    text("Esc — закрыть",W-170,88,0x444444);
    int y=92;
    if (!strcmp(a->title,"Настройки системы")){
        card(28,y,W-56,86,"Цвет рабочего стола"); for(int i=0;i<3;i++){ int cx=70+i*64; fill_rect(cx-20,y+34,40,40,DESK_RGB[i]); bevel(cx-20,y+34,40,40,1); if(cfg_desk==i){ fg(0x000000); XDrawRectangle(dpy,backbuf,gc,cx-24,y+30,48,48); } add_hit(cx-24,y+30,48,48,ACT_SETTING,i);} y+=100;
        char b[160]; snprintf(b,sizeof b,"Анимации: %s      30 FPS от батареи (X550EA): %s",cfg_anim?"ВКЛ":"ВЫКЛ",cfg_fps_battery_save?"ВКЛ":"ВЫКЛ");
        card(28,y,W-56,58,b); add_hit(28,y,W/3,58,ACT_SETTING,20); add_hit(28+W/3,y,W/3,58,ACT_SETTING,21); y+=72;
        text("Клик переключает параметр. Сохраняется в ~/.config/xross/xross.conf",28,y+20,0x444444);
    } else if (!strcmp(a->title,"Голосовое управление")){
        card(28,y,W-56,220,NULL); text_c("Голосовое управление (Vosk, офлайн)",W/2,y+44,0x000000);
        text_c("Клавиша V в оболочке · в фоне работает xross-voiced",W/2,y+78,0x333333);
        text_c("«открой музыку» · «вниз» · «выбери» · «домой» · «который час» · «помощь»",W/2,y+112,0xb03060);
        text_c("Демон пишет фразы в " VOICE_FIFO_PATH " — оболочка читает их без блокировок",W/2,y+146,0x555555);
    } else if (!strcmp(a->title,"О системе")){
        card(28,y,W-56,280,NULL); char b[256];
        snprintf(b,sizeof b,"XrossOS %s «%s» · оболочка C/Xlib (X11/XLibre) · только libX11",XROSS_VERSION,XROSS_CODENAME); text(b,48,y+36,0x000000);
        text("Хост-референс: ASUS X550EA — AMD A4-5000 (Jaguar, btver2), Radeon HD 8330 (GCN1, radeonsi)",48,y+68,0x333333);
        text("Панель 1366x768 · 4 ГБ DDR3L · рендер программный Pixmap, без композитора",48,y+96,0x333333);
        snprintf(b,sizeof b,"Экран: %dx%d · FPS сейчас: %d (батарея: экономия %s)",W,H,target_fps(),cfg_fps_battery_save?"вкл":"выкл"); text(b,48,y+124,0x333333);
        text("Все внешние приложения открываются в окнах с рамкой: закрытие, разворачивание, перетаскивание",48,y+152,0x006644);
        text("Ссылки на docs/x550ea.md — тюнинг ядра, amdgpu SI, zram, BFQ для HDD",48,y+180,0x555555);
    } else if (!strcmp(a->title,"Фотографии")){
        const unsigned g[6]={0xff5c7a,0x3366cc,0x8a6d00,0x7c5cff,0x38d39f,0xff8a5c};
        for(int i=0;i<6;i++){ int x=28+(i%3)*((W-56)/3), yy=y+(i/3)*190, w=(W-70)/3; fill_rect(x,yy,w,176,g[i]); text_c("Фото",x+w/2,yy+96,0x000000); add_hit(x,yy,w,176,ACT_STORE,99); }
    } else if (!strcmp(a->title,"Музыка")){
        static const char *tr[3]={"Волна океана","Неоновый город","Звёздная пыль"};
        for(int i=0;i<3;i++){ card(28,y,W-56,58,tr[i]); char b[32]; snprintf(b,sizeof b,"%s",music_track==i?"■ играет":"▶ слушать"); text(b,W-170,y+36,0xb03060); add_hit(28,y,W-56,58,ACT_TRACK,i); y+=70; }
        /* эквалайзер: дёшевые прямоугольники по синусам */
        for(int i=0;i<24;i++){ int h=music_track>=0? (int)(30+70*fabs(sin(t*6+i))):8; fill_rect(40+i*26,H-60-h,16,h,0xb03060); }
        if (music_track>=0) text("Звук на реальной ОС: файлы открывает mpv в окне; здесь — визуализация",28,H-80,0x555555);
    } else if (!strcmp(a->title,"Магазин")){
        static const char *st[4]={"Telegram","RetroArch (уже есть)","Kodi (уже есть)","GIMP"};
        for(int i=0;i<4;i++){ card(28,y,W-56,64,st[i]); char b[64]; snprintf(b,sizeof b,"%s",store_progress[i]>=100?"установлено":store_progress[i]>0?"установка…":"Установить (apk)"); text(b,W-260,y+40,0x8a6d00);
            if(store_progress[i]>0&&store_progress[i]<100){ store_progress[i]+=2; fill_rect(48,y+46,(W-120)*store_progress[i]/100,6,0x3366cc); if(store_progress[i]>=100) toast_set("Установлено: значок появится на рабочем столе после перезапуска оболочки"); }
            add_hit(28,y,W-56,64,ACT_STORE,i); y+=76; }
    } else if (!strcmp(a->title,"Файлы")){
        struct dirent **list=NULL; const char *home=getenv("HOME")?getenv("HOME"):".";
        int n=scandir(home,&list,NULL,alphasort); int shown=0;
        for(int i=0;i<n&&shown<12;i++){ if(list[i]->d_name[0]=='.'){ free(list[i]); continue; } card(28,y,W-56,50,list[i]->d_name); free(list[i]); y+=60; shown++; }
        free(list); if(!shown) text("Каталог пуст или недоступен",28,y+20,0x555555);
    } else if (!strcmp(a->title,"Заметки")){
        card(28,y,W-56,H-140,"Печатайте — сохраняется автоматически (Esc — выход)");
        /* перенос по словам не делаем: режем на строки по 90 символов для экрана */
        char tmp[4096]; snprintf(tmp,sizeof tmp,"%s",notes_buf); int yy=y+52;
        for(char *p=tmp; *p && yy<H-40;){ char *nl=strchr(p,'\n'); int len=nl? (int)(nl-p):(int)strlen(p); if(len>90)len=90; char line[96]; memcpy(line,p,len); line[len]=0; text(line,48,yy,0x000000); yy+=22; p+= (nl&&len==(int)(nl-p))? len+1:len; if(!nl&&len>=(int)strlen(p)) break; }
        if(!notes_buf[0]) text("Пустая заметка…",48,y+52,0x555555);
    } else {
        card(28,y,W-56,120,a->sub); text("Встроенное окно. Внешние приложения открываются в окнах с рамкой.",48,y+52,0x333333);
    }
}

static void draw(double t){
    nhits=0;
    if (mode==MODE_BOOT) draw_boot(t-boot_t0);
    else if (mode==MODE_APP && open_app>=0) draw_app(t);
    else draw_desktop(t);
    draw_topbar(); draw_deskbar();
    if (now_sec()<toast_until){ int w=text_w(toast_msg)+36; fill_rect(W/2-w/2,34,w,30,0xdddddd); bevel(W/2-w/2,34,w,30,1); text_c(toast_msg,W/2,54,0x000000); }
    XCopyArea(dpy,backbuf,shell_win,gc,0,0,W,H,0,0); XFlush(dpy);
}

/* ───────────────────────── ввод ───────────────────────── */
static void hit_click(int x,int y){
    static int last_item=-1; static double last_when;
    for(int i=nhits-1;i>=0;i--){ Hit *h=&hits[i]; if(x>=h->x&&x<h->x+h->w&&y>=h->y&&y<h->y+h->h){
        switch(h->action){
            case ACT_ITEM: { double nw=now_sec();
                if (item_idx==h->arg && last_item==h->arg && nw-last_when<0.45) open_app_idx(h->arg);
                item_idx=h->arg; last_item=h->arg; last_when=nw; break; }
            case ACT_MENU: menu_open = (menu_open==h->arg? 0:h->arg); break;
            case ACT_MENUITEM: { int code=h->arg; menu_open=0;
                if (code==100) open_app_idx(0);
                else if (code==101) open_app_idx(1);
                else if (code==102) open_app_idx(2);
                else if (code==200) do_select();
                else if (code==201) do_back();
                else if (code>=300 && code<303){ cfg_desk=code-300; config_save(); }
                else if (code==400){ mode=MODE_DESKTOP; XRaiseWindow(dpy,shell_win); }
                else if (code==401 && open_app>=0){ mode=MODE_APP; internal_min=0; }
                else if (code==402 && frame_win){ frame_hidden=0; XMapRaised(dpy,frame_win); if(client_win!=None) XSetInputFocus(dpy,client_win,RevertToParent,CurrentTime); }
                break; }
            case ACT_MINWIN: if (mode==MODE_APP){ internal_min=1; mode=MODE_DESKTOP; } break;
            case ACT_DESK:
                if (h->arg==1 && open_app>=0){ if (mode==MODE_APP && !internal_min){ internal_min=1; mode=MODE_DESKTOP; } else { internal_min=0; mode=MODE_APP; } }
                else if (h->arg==2 && frame_win){ if (frame_hidden){ frame_hidden=0; XMapRaised(dpy,frame_win); if(client_win!=None) XSetInputFocus(dpy,client_win,RevertToParent,CurrentTime); } else { frame_hidden=1; XUnmapWindow(dpy,frame_win); } }
                break;
            case ACT_CLOSEWIN: do_back(); break;
            case ACT_SETTING:
                if(h->arg<3) cfg_desk=h->arg;
                else if(h->arg==20) cfg_anim=!cfg_anim; else if(h->arg==21) cfg_fps_battery_save=!cfg_fps_battery_save;
                config_save(); toast_set("Настройки сохранены"); break;
            case ACT_TRACK: music_track = (music_track==h->arg? -1:h->arg); music_t0=now_sec(); break;
            case ACT_STORE: if(store_progress[h->arg]==0) store_progress[h->arg]=1; break;
        } return; } }
    menu_open=0;
    if (mode==MODE_BOOT) mode=MODE_DESKTOP;
}
static void key_press(XEvent *ev){
    char buf[64]; KeySym ks; XComposeStatus cs; int len=XLookupString(&ev->xkey,buf,sizeof buf,&ks,&cs); (void)len;
    if (mode==MODE_APP && open_app>=0 && !strcmp(APPS[open_app].title,"Заметки") && ks!=XK_Escape){
        if (ks==XK_BackSpace){ size_t n=strlen(notes_buf); if(n) notes_buf[n-1]=0; }
        else if (ks==XK_Return) strncat(notes_buf,"\n",sizeof notes_buf-strlen(notes_buf)-1);
        else if (len>0){ buf[len]=0; strncat(notes_buf,buf,sizeof notes_buf-strlen(notes_buf)-1); }
        notes_save(); return;
    }
    if (ks==XK_Tab && (ev->xkey.state&Mod1Mask)){
        if (frame_win){ frame_hidden=0; XMapRaised(dpy,frame_win); if(client_win!=None) XSetInputFocus(dpy,client_win,RevertToParent,CurrentTime); }
        else if (open_app>=0){ internal_min=0; mode=MODE_APP; }
        return;
    }
    switch(ks){
        case XK_Escape: do_back(); break;
        case XK_Return: case XK_KP_Enter: do_select(); break;
        case XK_Left: do_move(-1,0); break; case XK_Right: do_move(1,0); break;
        case XK_Up: do_move(0,-1); break; case XK_Down: do_move(0,1); break;
        case XK_v: case XK_V: toast_set("Голос: говорите… (демон xross-voiced)"); break;
        default: break;
    }
}
/* ───────────────────────── главный цикл ───────────────────────── */
static void usage(FILE *f){ fprintf(f,"XrossOS shell %s (%s) — C/Xlib оболочка для X11/XLibre\nИспользование: xross [--version] [--help]  (нужен DISPLAY)\nОптимизация по умолчанию: ASUS X550EA, %dx%d, %d/%d FPS\n",XROSS_VERSION,XROSS_CODENAME,X550EA_WIDTH,X550EA_HEIGHT,FPS_AC,FPS_BATTERY); }

int main(int argc,char **argv){
    if (argc>1 && (!strcmp(argv[1],"--version")||!strcmp(argv[1],"-v"))){ printf("xross %s (%s)\n",XROSS_VERSION,XROSS_CODENAME); return 0; }
    if (argc>1 && (!strcmp(argv[1],"--help")||!strcmp(argv[1],"-h"))){ usage(stdout); return 0; }
    signal(SIGCHLD,SIG_IGN); /* дети-подпроцессы не плодят зомби; статус внешних приложений нам не нужен */
    config_load(); notes_load(); srand(time(NULL));
    setlocale(LC_ALL,"");
    dpy=XOpenDisplay(NULL);
    if(!dpy){ fprintf(stderr,"xross: не удалось открыть DISPLAY (нужен X11/XLibre сервер)\n"); return 1; }
    screen_no=DefaultScreen(dpy); root_win=RootWindow(dpy,screen_no); cmap=DefaultColormap(dpy,screen_no);
    W=DisplayWidth(dpy,screen_no); H=DisplayHeight(dpy,screen_no);
    /* шрифт с кириллицей через FontSet (DejaVu в образе ОС) */
    char **missing; int nmissing; char *defstr;
    fontset=XCreateFontSet(dpy,"-*-dejavu sans-medium-r-normal--20-*-*-*-*-*-iso10646-1,-*-*-medium-r-normal--20-*-*-*-*-*-iso10646-1,*",&missing,&nmissing,&defstr);
    if(missing) XFreeStringList(missing);
    if(!fontset) fixedfont=XLoadQueryFont(dpy,"fixed");

    arrow_cursor=make_arrow_cursor();
    if (arrow_cursor) XDefineCursor(dpy,root_win,arrow_cursor);
    XSetWindowAttributes attr; attr.override_redirect=True; attr.background_pixel=px(0x05070f); if(arrow_cursor){ attr.cursor=arrow_cursor; }
    shell_win=XCreateWindow(dpy,root_win,0,0,W,H,0,CopyFromParent,InputOutput,CopyFromParent,CWOverrideRedirect|CWBackPixel|(arrow_cursor?CWCursor:0),&attr);
    XStoreName(dpy,shell_win,"XrossOS");
    XSelectInput(dpy,shell_win,ExposureMask|KeyPressMask|ButtonPressMask|PointerMotionMask|StructureNotifyMask);
    XMapRaised(dpy,shell_win);
    /* WM-функция: перехватываем появление чужих окон ПОСЛЕ показа своего */
    XSelectInput(dpy,root_win,SubstructureRedirectMask|SubstructureNotifyMask|KeyPressMask);
    /* Выход из ЛЮБОГО внешнего приложения: Ctrl+Esc, Home (аналог кнопки PS),
     * Super и F9 — глобальные захваты, работают при любом фокусе */
    XGrabKey(dpy,XKeysymToKeycode(dpy,XK_Escape),ControlMask,root_win,True,GrabModeAsync,GrabModeAsync);
    XGrabKey(dpy,XKeysymToKeycode(dpy,XK_Home),AnyModifier,root_win,True,GrabModeAsync,GrabModeAsync);
    XGrabKey(dpy,XKeysymToKeycode(dpy,XK_Super_L),AnyModifier,root_win,True,GrabModeAsync,GrabModeAsync);
    XGrabKey(dpy,XKeysymToKeycode(dpy,XK_F9),AnyModifier,root_win,True,GrabModeAsync,GrabModeAsync);

    gc=XCreateGC(dpy,shell_win,0,NULL);
    backbuf=XCreatePixmap(dpy,root_win,W,H,DefaultDepth(dpy,screen_no));
    boot_t0=now_sec();

    mkfifo(VOICE_FIFO_PATH,0666); voice_fd=open(VOICE_FIFO_PATH,O_RDONLY|O_NONBLOCK);
    int xfd=ConnectionNumber(dpy);
    double last_battery_check=0; (void)last_battery_check;

    for(;;){
        double frame_start=now_sec();
        /* события X без busy-loop: select до следующего кадра */
        while (XPending(dpy)){
            XEvent ev; XNextEvent(dpy,&ev);
            switch(ev.type){
                case Expose: if (ev.xexpose.window==frame_win) draw_frame(); break;
                case ConfigureNotify: if(ev.xconfigure.window==shell_win){ W=ev.xconfigure.width; H=ev.xconfigure.height; XFreePixmap(dpy,backbuf); backbuf=XCreatePixmap(dpy,root_win,W,H,DefaultDepth(dpy,screen_no)); if(bg_cache){XFreePixmap(dpy,bg_cache); bg_cache=None;} } break;
                case ButtonPress:
                    if (ev.xbutton.window==frame_win){
                        int bx=ev.xbutton.x, by=ev.xbutton.y;
                        if (by>=3 && by<=19 && bx>=14 && bx<=34){ close_client(); break; }
                        if (by>=3 && by<=19 && bx>=fw-52 && bx<fw-34){ frame_hidden=1; XUnmapWindow(dpy,frame_win); break; }
                        if (by>=3 && by<=19 && bx>=fw-34){ /* развернуть/восстановить */
                            if (!client_max){ save_x=fx; save_y=fy; save_w=fw; save_h=fh; fx=0; fy=24; fw=W; fh=H-50; client_max=1; }
                            else { fx=save_x; fy=save_y; fw=save_w; fh=save_h; client_max=0; }
                            frame_apply(); break; }
                        if (by<TB && !client_max){ drag_on=1; drag_dx=ev.xbutton.x_root-fx; drag_dy=ev.xbutton.y_root-fy; }
                        break; }
                    hit_click(ev.xbutton.x, ev.xbutton.y); break;
                case ButtonRelease: drag_on=0; break;
                case MotionNotify:
                    if (drag_on && ev.xmotion.window==frame_win){ fx=ev.xmotion.x_root-drag_dx; fy=ev.xmotion.y_root-drag_dy; if(fy<24)fy=24; frame_apply(); }
                    break;
                case KeyPress: { KeySym ks0=XLookupKeysym(&ev.xkey,0);
                    int exit_key = ks0==XK_Home || ks0==XK_Super_L || ks0==XK_F9 ||
                                   ((ev.xkey.state&ControlMask) && ks0==XK_Escape);
                    if (exit_key && (client_win!=None || child_pid>0)){ close_client(); break; }
                    if (exit_key && mode==MODE_APP){ do_back(); break; }
                    key_press(&ev); break; }
                case MapRequest: { Window w=ev.xmaprequest.window;
                    if (w==frame_win){ XMapWindow(dpy,w); break; }
                    if (w!=shell_win){
                        client_win=w;
                        fw=W*72/100; fh=H*72/100; fx=(W-fw)/2; fy=40; client_max=0;
                        XSetWindowAttributes fa; fa.override_redirect=True; fa.background_pixel=px(0xdddddd);
                        frame_win=XCreateWindow(dpy,root_win,fx,fy,fw,fh,1,CopyFromParent,InputOutput,CopyFromParent,CWOverrideRedirect|CWBackPixel,&fa);
                        XStoreName(dpy,frame_win,"XrossWindow");
                        XSelectInput(dpy,frame_win,ExposureMask|ButtonPressMask|ButtonReleaseMask|PointerMotionMask);
                        if (arrow_cursor) XDefineCursor(dpy,frame_win,arrow_cursor);
                        XSelectInput(dpy,w,StructureNotifyMask);
                        XSetWindowBorderWidth(dpy,w,0);
                        XReparentWindow(dpy,w,frame_win,1,TB);
                        frame_apply();
                        XMapWindow(dpy,frame_win); XMapWindow(dpy,w);
                        XRaiseWindow(dpy,frame_win);
                        XSetInputFocus(dpy,w,RevertToParent,CurrentTime);
                    }
                    else XMapWindow(dpy,w);
                    break; }
                case ConfigureRequest: { XWindowChanges wc; wc.border_width=0;
                    if (ev.xconfigurerequest.window==client_win && frame_win){
                        fw=ev.xconfigurerequest.width+2; fh=ev.xconfigurerequest.height+TB+2;
                        if (fw<240) fw=240; if (fh<160) fh=160; frame_apply();
                    } else {
                        wc.x=ev.xconfigurerequest.x; wc.y=ev.xconfigurerequest.y;
                        wc.width=ev.xconfigurerequest.width; wc.height=ev.xconfigurerequest.height;
                        XConfigureWindow(dpy,ev.xconfigurerequest.window,CWX|CWY|CWWidth|CWHeight|CWBorderWidth,&wc);
                    } break; }
                case DestroyNotify:
                    if (ev.xdestroywindow.window==client_win){
                        client_win=None; child_pid=-1;
                        if (frame_win){ XDestroyWindow(dpy,frame_win); frame_win=None; }
                        client_max=0; drag_on=0; XMapRaised(dpy,shell_win); XSetInputFocus(dpy,shell_win,RevertToParent,CurrentTime);
                        if(mode==MODE_APP) mode=MODE_DESKTOP; }
                    break;
                default: break;
            }
        }
        /* внешний процесс умер, а окна не создал (нет бинарника) — вернуть оболочку */
        if (child_pid>0 && client_win==None){ if (kill(child_pid,0)<0 && errno==ESRCH){ child_pid=-1; XMapRaised(dpy,shell_win); toast_set("Приложение закрылось или не запустилось (см. /var/log/xross.log)"); } }
        voice_poll();
        double k = cfg_anim? 0.22 : 1.0;
        anim_item += (item_idx-anim_item)*k; if (fabs(item_idx-anim_item)<0.001) anim_item=item_idx; (void)anim_item;
        double t=frame_start;
        draw(t);
        frame_no++;
        double budget=1.0/target_fps(); double spent=now_sec()-frame_start;
        if (spent<budget){ struct timespec ts={0,(long)((budget-spent)*1e9)}; /* select вместо sleep: не проспим события */
            fd_set rf; FD_ZERO(&rf); FD_SET(xfd,&rf); if(voice_fd>=0) FD_SET(voice_fd,&rf);
            struct timeval tv={0,(long)((budget-spent)*1e6)}; (void)ts; select((xfd>voice_fd?xfd:voice_fd)+1,&rf,NULL,NULL,&tv); }
    }
    return 0;
}
