/* XrossOS shell — C/Xlib. Референсы: PS3/PSP (XMB), PS Vita (сферы),
 * Dreamcast (заставка со «свёрлом», орбитальное меню из 4 сфер).
 *
 * xross одновременно:
 *  1) полноэкранная оболочка (override-redirect окно на весь экран);
 *  2) минимальный WM: SubstructureRedirect на root — КАЖДОЕ чужое окно
 *     принудительно разворачивается на весь экран без рамок. Это X11-
 *     аналог поведения Cage в прошлой (Wayland/JS) версии;
 *  3) клиент голосового демона xross-voiced (команды из FIFO);
 *  4) читатель геймпада /dev/input/js0 (Linux joystick API).
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
#ifdef __linux__
#include <linux/joystick.h>
#endif
#include "xross.h"

/* ───────────────────────── типы и состояние ───────────────────────── */
typedef struct { int x, y, w, h, action, arg; } Hit;
typedef struct { const char *cat, *title, *sub, *glyph; unsigned rgb; int ext; const char *cmd; } AppDef;

enum { MODE_BOOT, MODE_XMB, MODE_DC, MODE_APP };
enum { APP_NONE = -1 };
/* action-коды хитбоксов */
enum { ACT_CAT, ACT_ITEM, ACT_SETTING, ACT_TILE, ACT_TRACK, ACT_STORE, ACT_DC_ORBIT, ACT_CLOSE };

static Display *dpy; static Window root_win, shell_win; static Pixmap backbuf;
static GC gc; static XFontSet fontset; static XFontStruct *fixedfont;
static int W, H, screen_no;
static Colormap cmap;

static int mode = MODE_BOOT;
static int cat_idx = 3, item_idx = 0;          /* старт: «Музыка» */
static double anim_cat = 3.0, anim_item = 0.0; /* сглаженные позиции XMB */
static double boot_t0;
static int open_app = APP_NONE;                /* индекс в APPS или -1 */
static Window client_win = None;               /* чужое полноэкранное окно */
static pid_t child_pid = -1;
static char toast_msg[256]; static double toast_until;
static Hit hits[128]; static int nhits;
static long frame_no;

/* настройки (сохраняются в CONFIG_PATH) */
static int cfg_hue = 222;        /* PS3-волна */
static int cfg_swirl = 0;        /* 0=синий EU, 1=оранжевый JP, 2=красный US (Dreamcast) */
static int cfg_anim = 1, cfg_fps_battery_save = 1, cfg_dc_default = 0;
static unsigned SWIRL_RGB[3] = { 0x2b6cb0, 0xf07d18, 0xd63a2f };

static const char *CATS[][2] = {
    {"users","Пользователи"},{"settings","Настройки"},{"photo","Фото"},{"music","Музыка"},
    {"video","Видео"},{"game","Игры"},{"network","Сеть"},{"apps","Приложения"},
};
#define NCATS 8
static const AppDef APPS[] = {
    {"users","Профиль игрока","Гость · уровень 1","U",0x7c5cff,0,NULL},
    {"users","Достижения","3 из 42","T",0xf5b301,0,NULL},
    {"settings","Настройки системы","Тема, свёрл, FPS, анимации","S",0x9aa7b4,0,NULL},
    {"settings","Голосовое управление","Vosk офлайн · клавиша V","V",0xff5c7a,0,NULL},
    {"settings","О системе","XrossOS " XROSS_VERSION " · ASUS X550EA","i",0x4cc9f0,0,NULL},
    {"photo","Фотографии","Галерея","P",0x38d39f,0,NULL},
    {"music","Музыка","Треки и эквалайзер","M",0xff7ac6,0,NULL},
    {"video","Видео (mpv)","Внешний плеер — на весь экран","▶",0xff8a5c,1,"mpv --idle --fs"},
    {"game","Пузыри","Мини-игра в стиле PS Vita / Dreamcast","B",0x4cc9f0,0,NULL},
    {"game","Змейка","Классика","S",0x38d39f,0,NULL},
    {"game","Ретро-игры (RetroArch)","Внешнее приложение","R",0xb089ff,1,"retroarch -f"},
    {"network","Браузер Firefox","Внешнее приложение","F",0x4cc9f0,1,"firefox-esr"},
    {"network","Магазин","Установка пакетов apk","+",0xf5b301,0,NULL},
    {"apps","Файлы","Домашний каталог","D",0xffd166,0,NULL},
    {"apps","Терминал (xterm)","Внешнее приложение",">",0xa7f3d0,1,"xterm -fa DejaVuSansMono -fs 13 -bg black -fg white"},
    {"apps","Заметки","Сохраняются в ~/.xross-notes","N",0xfef08a,0,NULL},
    {"apps","Kodi","Медиацентр, на весь экран","K",0x61afef,1,"kodi"},
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

/* сфера-иконка: PS Vita + Dreamcast (блик сверху-слева) */
static void sphere(int cx,int cy,int r,unsigned rgb,int focused){
    fill_circle(cx,cy,r, blend(rgb,0x000000,0.25));
    fill_circle(cx,cy,r-2, rgb);
    fill_circle(cx-r/3,cy-r/3,r/3, blend(rgb,0xffffff,0.55));
    fill_circle(cx-r/4,cy-r/4,r/6, blend(rgb,0xffffff,0.8));
    if (focused) draw_circle(cx,cy,r+5,0xffffff,2);
}

/* Dreamcast-свёрл: логарифмическая спираль полилинией */
static void draw_swirl(int cx,int cy,double maxr,double rot,unsigned rgb,int lw){
    XPoint pts[180]; int n=0; double turns=2.35;
    for (int i=0;i<170;i++){ double t=i/169.0, a=rot + t*turns*2*M_PI, r=maxr*pow(t,0.72)*(0.25+0.75*t);
        pts[n++]=(XPoint){(short)(cx+cos(a)*r),(short)(cy+sin(a)*r*0.96)}; }
    fg(rgb); XSetLineAttributes(dpy,gc,lw,LineSolid,CapRound,JoinRound); XDrawLines(dpy,backbuf,gc,pts,n,CoordModeOrigin);
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
    while (fscanf(f,"%63[^=]=%63s\n",k,v)==2){ if(!strcmp(k,"hue"))cfg_hue=atoi(v); else if(!strcmp(k,"swirl"))cfg_swirl=atoi(v)%3;
        else if(!strcmp(k,"anim"))cfg_anim=atoi(v); else if(!strcmp(k,"fps_battery_save"))cfg_fps_battery_save=atoi(v); else if(!strcmp(k,"dc_default"))cfg_dc_default=atoi(v); }
    fclose(f);
}
static void config_save(void){
    char p[512]; path_home(p,sizeof p,CONFIG_PATH); char d[512]; snprintf(d,sizeof d,"%s",p); char *s=strrchr(d,'/'); if(s){*s=0; mkdir(d,0755);} 
    /* ~/.config может не быть: создать поэтапно */
    char mk[600]; path_home(mk,sizeof mk,"/.config"); mkdir(mk,0755); path_home(mk,sizeof mk,"/.config/xross"); mkdir(mk,0755);
    FILE *f=fopen(p,"w"); if(!f) return;
    fprintf(f,"hue=%d\nswirl=%d\nanim=%d\nfps_battery_save=%d\ndc_default=%d\n",cfg_hue,cfg_swirl,cfg_anim,cfg_fps_battery_save,cfg_dc_default); fclose(f);
}

/* ───────────────────────── приложения: данные игр ───────────────────────── */
static struct { double x,y,r; int alive; double born; const char *g; } bubbles[10];
static int bubble_score;
static int snake_pts, snake_dir[2], snake_xy[64][2], snake_len, snake_food[2]; static double snake_last;
static char notes_buf[4096]; static int music_track=-1; static double music_t0;
static int store_progress[4];

static int apps_in_cat(int c){ int n=0; for(int i=0;i<NAPPS;i++) if(!strcmp(APPS[i].cat,CATS[c][0])) n++; return n; }
static int app_index(int c,int nth){ for(int i=0;i<NAPPS;i++) if(!strcmp(APPS[i].cat,CATS[c][0]) && nth--==0) return i; return -1; }

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
    toast_set("Запуск на весь экран…");
    pid_t pid=fork();
    if (pid==0){ setsid();
        /* через sh -c: раскрываются ~, переменные и кавычки в командах реестра */
        execl("/bin/sh","sh","-c",cmdline,(char*)NULL); _exit(127); }
    child_pid=pid;
    XUnmapWindow(dpy,shell_win); XFlush(dpy);
    /* смерть ребёнка ловит главный цикл по kill(pid,0): при SIG_IGN waitpid не работает */
}
static void close_client(void){
    if (client_win!=None){ XKillClient(dpy,client_win); client_win=None; }
    if (child_pid>0){ kill(child_pid,SIGTERM); }
    XMapRaised(dpy,shell_win); mode = cfg_dc_default?MODE_DC:MODE_XMB; open_app=APP_NONE;
}

static void open_app_idx(int ai){
    if (ai<0) return;
    if (APPS[ai].ext){ launch_external(APPS[ai].cmd, APPS[ai].title); return; }
    open_app=ai; mode=MODE_APP;
    if (!strcmp(APPS[ai].title,"Пузыри")){ bubble_score=0; for(int i=0;i<10;i++){ bubbles[i].alive=1; bubbles[i].x=80+rand()%(W>240?W-240:200); bubbles[i].y=140+rand()%(H>300?H-300:200); bubbles[i].r=26+rand()%30; bubbles[i].born=now_sec(); bubbles[i].g="*"; } }
    if (!strcmp(APPS[ai].title,"Змейка")){ snake_len=3; snake_pts=0; snake_dir[0]=1; snake_dir[1]=0; snake_xy[0][0]=8; snake_xy[0][1]=8; snake_food[0]=15; snake_food[1]=10; snake_last=now_sec(); }
    char b[128]; snprintf(b,sizeof b,"%s — на весь экран · Esc — выход",APPS[ai].title); toast_set(b);
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
        struct { const char *w; const char *title; } syn[]={{"музык","Музыка"},{"настрой","Настройки системы"},{"файл","Файлы"},{"термин","Терминал (xterm)"},{"брауз","Браузер Firefox"},{"игр","Пузыри"},{"замет","Заметки"},{"фото","Фотографии"}};
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
static void do_move(int dc,int di){
    if (mode==MODE_APP){ if(open_app>=0&&!strcmp(APPS[open_app].title,"Змейка")){ if(dc){snake_dir[0]=dc;snake_dir[1]=0;} if(di){snake_dir[0]=0;snake_dir[1]=di;} } return; }
    if (mode==MODE_DC){ if(dc) cat_idx=(cat_idx+dc+NCATS)%NCATS; return; }
    if (mode!=MODE_XMB) { mode=MODE_XMB; return; }
    if (dc){ cat_idx=(cat_idx+dc+NCATS)%NCATS; item_idx=0; }
    if (di){ int n=apps_in_cat(cat_idx); if(n) item_idx=(item_idx+di+n)%n; }
}
static void do_select(void){
    if (mode==MODE_BOOT){ mode=cfg_dc_default?MODE_DC:MODE_XMB; return; }
    if (mode==MODE_DC){ /* орбита Dreamcast: 4 сферы = Играть/Файлы/Музыка/Настройки */
        static const char *orbit_cat[4]={"game","apps","music","settings"};
        for(int i=0;i<NCATS;i++) if(!strcmp(CATS[i][0],orbit_cat[cat_idx%4])){ cat_idx=i; break; } item_idx=0; mode=MODE_XMB; return; }
    if (mode==MODE_XMB) open_app_idx(app_index(cat_idx,item_idx));
}
static void do_back(void){
    if (mode==MODE_APP){ open_app=APP_NONE; mode=cfg_dc_default?MODE_DC:MODE_XMB; if(music_track>=0) music_track=-1; notes_save(); return; }
    if (mode==MODE_DC) mode=MODE_XMB;
    if (client_win!=None||child_pid>0) close_client();
}

/* ───────────────────────── фон: градиент + волна PS3 ───────────────────────── */
static Pixmap bg_cache; static int bg_hue_cache=-1, bg_w, bg_h;
static unsigned hue_rgb(int hue,double l,double s){ /* HSL->RGB упрощённо через blend палитры */
    static const unsigned pal[6]={0xff3b30,0xffcc00,0x35c759,0x32ade6,0x5856d6,0xff2d55};
    double pos=(hue%360)/60.0; int i=(int)pos; unsigned c=blend(pal[i%6],pal[(i+1)%6],pos-i);
    return blend(c, l>0.5?0xffffff:0x05070f, l>0.5?(l-0.5)*1.2:(0.5-l)*1.4 + (1-s)*0.2);
}
static void render_bg(void){
    if (bg_cache && bg_hue_cache==cfg_hue && bg_w==W && bg_h==H) { XCopyArea(dpy,bg_cache,backbuf,gc,0,0,W,H,0,0); return; }
    if (bg_cache) XFreePixmap(dpy,bg_cache);
    bg_cache=XCreatePixmap(dpy,root_win,W,H,DefaultDepth(dpy,screen_no)); bg_hue_cache=cfg_hue; bg_w=W; bg_h=H;
    /* вертикальный градиент полосами по 4px: дёшево даже на Jaguar */
    GC g2=XCreateGC(dpy,bg_cache,0,NULL);
    for(int y=0;y<H;y+=4){ double t=(double)y/H; unsigned c=hue_rgb(cfg_hue, 0.10+0.10*sin(t*2.2), 0.8);
        XSetForeground(dpy,g2,px(c)); XFillRectangle(dpy,bg_cache,g2,0,y,W,4); }
    XFreeGC(dpy,g2); XCopyArea(dpy,bg_cache,backbuf,gc,0,0,W,H,0,0);
    /* блёстки PS3 */
    srand(42); for(int i=0;i<60;i++){ int x=rand()%W,y=rand()%H; fill_circle(x,y,1,blend(hue_rgb(cfg_hue,.8,.6),0xffffff,.5)); }
    srand(time(NULL));
}

/* ───────────────────────── отрисовка экранов ───────────────────────── */
static void draw_topbar(void){
    fill_rect(0,0,W,44,0x0b1020);
    text("XrossOS " XROSS_VERSION " «" XROSS_CODENAME "»",22,29,0xffffff);
    time_t tt=time(NULL); char b[96]; struct tm *tmv=localtime(&tt);
    int bat=battery_pct();
    snprintf(b,sizeof b,"%s  %02d:%02d", bat>=0?"BAT":"", tmv->tm_hour,tmv->tm_min);
    if (bat>=0){ char bb[32]; snprintf(bb,sizeof bb,"%d%%",bat); text(bb,W-170,29,0xa7f3d0); }
    text(b,W-120,29,0xffffff);
    text("🎤 V",W-230,29,0xff8fa8); /* emoji может отсутствовать в шрифте — рядом подпись V */
    text("V=голос",W-300,29,0xff8fa8);
}

static void draw_boot(double t){
    fill_rect(0,0,W,H,0xf4f6f8); /* белая заставка Dreamcast */
    double p = t<1.6 ? t/1.6 : 1.0; double ease=1-pow(1-p,3);
    draw_swirl(W/2,H/2-40, 150*ease, t*1.8, SWIRL_RGB[cfg_swirl], 7);
    text_c("XrossOS",W/2,H/2+150,0x203040);
    text_c("Нажмите Enter · Press Start",W/2,H/2+182,0x5a6a7a);
    if (t>3.2) mode=cfg_dc_default?MODE_DC:MODE_XMB;
}

static void draw_xmb(double t){
    render_bg();
    /* волна PS3: 3 полилинии, шаг 8px (на 1366 — ~170 точек) */
    for(int layer=0;layer<3;layer++){
        XPoint pts[220]; int n=0; double amp=34+layer*16, yb=H*0.44+layer*14;
        for(int x=0;x<=W&&n<220;x+=8){ double y=yb+sin(x/300.0+t*(0.9-layer*0.18))*amp+sin(x/170.0-t*0.6)*amp*0.4; pts[n++]=(XPoint){(short)x,(short)y}; }
        fg(hue_rgb(cfg_hue+layer*30,.62-layer*.08,.9)); XSetLineAttributes(dpy,gc,layer==0?2:2+layer*2,LineSolid,CapRound,JoinRound); XDrawLines(dpy,backbuf,gc,pts,n,CoordModeOrigin);
    }
    draw_swirl(W-120,92,42,t*0.25,SWIRL_RGB[cfg_swirl],3); /* маленький свёрл Dreamcast в углу */
    nhits=0;
    /* категории: фокус всегда в центре (как PS3) */
    for(int i=0;i<NCATS;i++){
        double d=i-anim_cat; if (fabs(d)>4.2) continue;
        int cx=W/2+(int)(d*148), cy=(int)(H*0.34), r=(int)(44-fabs(d)*7); if(r<18)r=18;
        unsigned col=hue_rgb(cfg_hue+i*36,.58,.75);
        sphere(cx,cy,r,col,fabs(d)<0.5);
        char g[2]={CATS[i][1][0],0}; /* первая буква (для кириллицы ненадёжно) — используем индексную метку ниже */
        (void)g;
        if (fabs(d)<0.5) text_c(CATS[i][1],cx,cy+r+26,0xffffff);
        else { char lb[64]; snprintf(lb,sizeof lb,"%s",CATS[i][1]); text_c(lb,cx,cy+r+22,blend(col,0xffffff,.3)); }
        add_hit(cx-64,cy-64,128,128,ACT_CAT,i);
    }
    /* вертикальный список */
    int n=apps_in_cat(cat_idx);
    for(int i=0;i<n;i++){
        double d=i-anim_item; if (fabs(d)>4.5) continue;
        int ai=app_index(cat_idx,i); int y=(int)(H*0.52+d*64); int x=W/2-230+(int)(fabs(d)*10);
        int focused=fabs(d)<0.5;
        if (focused) fill_rect(x-14,y-34,480,56,0x1b2742);
        sphere(x+10,y-8,(focused?24:19),APPS[ai].rgb,focused);
        text(APPS[ai].title,x+48,y,focused?0xffffff:0xc8d2e0);
        text(APPS[ai].sub,x+48,y+20,0x8fa0b8);
        add_hit(x-14,y-34,480,56,ACT_ITEM,i);
    }
    (void)n;
}

static void draw_dc(double t){
    render_bg();
    draw_swirl(W/2,H/2-30,120, t*0.35, SWIRL_RGB[cfg_swirl],6);
    static const char *names[4]={"Играть","Файлы","Музыка","Настройки"};
    static const unsigned cols[4]={0x4cc9f0,0xffd166,0xff7ac6,0x9aa7b4};
    nhits=0;
    for(int i=0;i<4;i++){
        double a=t*0.22 + i*M_PI/2.0; int cx=W/2+(int)(cos(a)*210), cy=H/2-30+(int)(sin(a)*92);
        int sel=(cat_idx%4==i); sphere(cx,cy,sel?46:38,cols[i],sel); text_c(names[i],cx,cy+70,0xffffff);
        add_hit(cx-52,cy-52,104,104,ACT_DC_ORBIT,i);
    }
    text_c("Dreamcast-меню · Enter — открыть раздел · F1 — XMB",W/2,H-56,0xdbe7f5);
}

static void card(int x,int y,int w,int h,const char *title){
    fill_rect(x,y,w,h,0x16203a); if(title) text(title,x+16,y+26,0xffffff);
}
static void draw_app(double t){
    fill_rect(0,0,W,H,0x0a1122);
    const AppDef *a=&APPS[open_app];
    text(a->title,28,42,0xffffff); text("Esc — назад",W-150,42,0x8fa0b8);
    int y=76;
    if (!strcmp(a->title,"Настройки системы")){
        static const int hues[6]={222,265,190,330,150,20};
        card(28,y,W-56,86,"Тема волны PS3"); for(int i=0;i<6;i++){ int cx=70+i*64; fill_circle(cx,y+58,20,hue_rgb(hues[i],.55,.85)); if(cfg_hue==hues[i]) draw_circle(cx,y+58,24,0xffffff,2); add_hit(cx-24,y+34,48,48,ACT_SETTING,i);} y+=100;
        card(28,y,W-56,64,"Свёрл Dreamcast: EU синий / JP оранжевый / US красный"); for(int i=0;i<3;i++){ int cx=70+i*64; fill_circle(cx,y+42,16,SWIRL_RGB[i]); if(cfg_swirl==i) draw_circle(cx,y+42,20,0xffffff,2); add_hit(cx-22,y+20,44,44,ACT_SETTING,10+i);} y+=78;
        char b[128]; snprintf(b,sizeof b,"Анимации: %s   ·   30 FPS от батареи (X550EA): %s   ·   Старт в меню Dreamcast: %s",cfg_anim?"ВКЛ":"ВЫКЛ",cfg_fps_battery_save?"ВКЛ":"ВЫКЛ",cfg_dc_default?"ДА":"НЕТ");
        card(28,y,W-56,58,b); add_hit(28,y,W/3,58,ACT_SETTING,20); add_hit(28+W/3,y,W/3,58,ACT_SETTING,21); add_hit(28+2*W/3,y,W/3-56,58,ACT_SETTING,22); y+=72;
        text("Клик по строке переключает параметр. Всё сохраняется в ~/.config/xross/xross.conf",28,y+20,0x8fa0b8);
    } else if (!strcmp(a->title,"Голосовое управление")){
        card(28,y,W-56,220,NULL); text_c("Голосовое управление (Vosk, офлайн)",W/2,y+44,0xffffff);
        text_c("Клавиша V в оболочке · в фоне работает xross-voiced",W/2,y+78,0xc8d2e0);
        text_c("«открой музыку» · «вниз» · «выбери» · «домой» · «который час» · «помощь»",W/2,y+112,0xff8fa8);
        text_c("Демон пишет фразы в " VOICE_FIFO_PATH " — оболочка читает их без блокировок",W/2,y+146,0x8fa0b8);
    } else if (!strcmp(a->title,"О системе")){
        card(28,y,W-56,280,NULL); char b[256];
        snprintf(b,sizeof b,"XrossOS %s «%s» · оболочка C/Xlib (X11/XLibre) · только libX11",XROSS_VERSION,XROSS_CODENAME); text(b,48,y+36,0xffffff);
        text("Хост-референс: ASUS X550EA — AMD A4-5000 (Jaguar, btver2), Radeon HD 8330 (GCN1, radeonsi)",48,y+68,0xc8d2e0);
        text("Панель 1366x768 · 4 ГБ DDR3L · рендер программный Pixmap, без композитора",48,y+96,0xc8d2e0);
        snprintf(b,sizeof b,"Экран: %dx%d · FPS сейчас: %d (батарея: экономия %s)",W,H,target_fps(),cfg_fps_battery_save?"вкл":"выкл"); text(b,48,y+124,0xc8d2e0);
        text("Все внешние приложения: xross как WM разворачивает каждое окно на весь экран",48,y+152,0xa7f3d0);
        text("Ссылки на docs/x550ea.md — тюнинг ядра, amdgpu SI, zram, BFQ для HDD",48,y+180,0x8fa0b8);
    } else if (!strcmp(a->title,"Фотографии")){
        const unsigned g[6]={0xff5c7a,0x4cc9f0,0xf5b301,0x7c5cff,0x38d39f,0xff8a5c};
        for(int i=0;i<6;i++){ int x=28+(i%3)*((W-56)/3), yy=y+(i/3)*190, w=(W-70)/3; fill_rect(x,yy,w,176,g[i]); text_c("Фото",x+w/2,yy+96,0xffffff); add_hit(x,yy,w,176,ACT_TILE,i); }
    } else if (!strcmp(a->title,"Музыка")){
        static const char *tr[3]={"Волна океана","Неоновый город","Звёздная пыль"};
        for(int i=0;i<3;i++){ card(28,y,W-56,58,tr[i]); char b[32]; snprintf(b,sizeof b,"%s",music_track==i?"■ играет":"▶ слушать"); text(b,W-170,y+36,0xff7ac6); add_hit(28,y,W-56,58,ACT_TRACK,i); y+=70; }
        /* эквалайзер: дёшевые прямоугольники по синусам */
        for(int i=0;i<24;i++){ int h=music_track>=0? (int)(30+70*fabs(sin(t*6+i))):8; fill_rect(40+i*26,H-60-h,16,h,0xff7ac6); }
        if (music_track>=0) text("Звук на реальной ОС: файлы открывает mpv/VLC на весь экран; здесь — визуализация",28,H-80,0x8fa0b8);
    } else if (!strcmp(a->title,"Пузыри")){
        char b[160]; snprintf(b,sizeof b,"Лопнуто: %d / 10 · клик или касание по сфере",bubble_score); text(b,28,y+8,0xffffff);
        for(int i=0;i<10;i++){ if(!bubbles[i].alive) continue; double bob=sin(t*2+i)*6; sphere((int)bubbles[i].x,(int)(bubbles[i].y+bob),(int)bubbles[i].r,0x4cc9f0,false); }
        /* хитбоксы сфер обновляются кликом через ACT_TILE с координатной проверкой в обработчике */
        add_hit(0,60,W,H-60,ACT_TILE,77);
    } else if (!strcmp(a->title,"Змейка")){
        const int cs=20, ox=W/2-260, oy=110; fill_rect(ox,oy,520,520,0x05070f);
        if (now_sec()-snake_last>0.13){ snake_last=now_sec();
            for(int i=snake_len-1;i>0;i--){ snake_xy[i][0]=snake_xy[i-1][0]; snake_xy[i][1]=snake_xy[i-1][1]; }
            snake_xy[0][0]+=snake_dir[0]; snake_xy[0][1]+=snake_dir[1];
            if(snake_xy[0][0]<0||snake_xy[0][1]<0||snake_xy[0][0]>=26||snake_xy[0][1]>=26){ snake_xy[0][0]=8; snake_xy[0][1]=8; snake_pts=0; snake_len=3; }
            if(snake_xy[0][0]==snake_food[0]&&snake_xy[0][1]==snake_food[1]){ snake_pts++; if(snake_len<63)snake_len++; snake_food[0]=rand()%26; snake_food[1]=rand()%26; }
        }
        fill_rect(ox+snake_food[0]*cs+3,oy+snake_food[1]*cs+3,cs-6,cs-6,0xff5c7a);
        for(int i=snake_len-1;i>=0;i--) fill_rect(ox+snake_xy[i][0]*cs+2,oy+snake_xy[i][1]*cs+2,cs-4,cs-4,0x38d39f);
        char b[160]; snprintf(b,sizeof b,"Очки: %d · стрелки или голос «вверх/вниз/влево/вправо»",snake_pts); text(b,ox,oy+548,0xffffff);
    } else if (!strcmp(a->title,"Магазин")){
        static const char *st[4]={"Telegram","RetroArch (уже есть)","Kodi (уже есть)","GIMP"};
        for(int i=0;i<4;i++){ card(28,y,W-56,64,st[i]); char b[64]; snprintf(b,sizeof b,"%s",store_progress[i]>=100?"установлено":store_progress[i]>0?"установка…":"Установить (apk)"); text(b,W-260,y+40,0xf5b301);
            if(store_progress[i]>0&&store_progress[i]<100){ store_progress[i]+=2; fill_rect(48,y+46,(W-120)*store_progress[i]/100,6,0x4cc9f0); if(store_progress[i]>=100) toast_set("Установлено: ярлык появится в XMB после перезапуска оболочки"); }
            add_hit(28,y,W-56,64,ACT_STORE,i); y+=76; }
    } else if (!strcmp(a->title,"Файлы")){
        struct dirent **list=NULL; const char *home=getenv("HOME")?getenv("HOME"):".";
        int n=scandir(home,&list,NULL,alphasort); int shown=0;
        for(int i=0;i<n&&shown<12;i++){ if(list[i]->d_name[0]=='.'){ free(list[i]); continue; } card(28,y,W-56,50,list[i]->d_name); free(list[i]); y+=60; shown++; }
        free(list); if(!shown) text("Каталог пуст или недоступен",28,y+20,0x8fa0b8);
    } else if (!strcmp(a->title,"Заметки")){
        card(28,y,W-56,H-140,"Печатайте — сохраняется автоматически (Esc — выход)");
        /* перенос по словам не делаем: режем на строки по 90 символов для экрана */
        char tmp[4096]; snprintf(tmp,sizeof tmp,"%s",notes_buf); int yy=y+52;
        for(char *p=tmp; *p && yy<H-40;){ char *nl=strchr(p,'\n'); int len=nl? (int)(nl-p):(int)strlen(p); if(len>90)len=90; char line[96]; memcpy(line,p,len); line[len]=0; text(line,48,yy,0xffffff); yy+=22; p+= (nl&&len==(int)(nl-p))? len+1:len; if(!nl&&len>=(int)strlen(p)) break; }
        if(!notes_buf[0]) text("Пустая заметка…",48,y+52,0x8fa0b8);
    } else if (!strcmp(a->title,"Профиль игрока")){
        card(28,y,W-56,170,NULL); text_c("Гость",W/2,y+52,0xffffff); text_c("Трофеев: 3 · время в системе: 12 ч · вход локальный",W/2,y+86,0xc8d2e0);
    } else if (!strcmp(a->title,"Достижения")){
        static const char *tr[4][2]={{"Первый запуск","Открыть XrossOS"},{"Голос!","Первая голосовая команда"},{"Меломан","Открыть музыку"},{"Ловец сфер","10 пузырей в мини-игре"}};
        for(int i=0;i<4;i++){ card(28,y,W-56,56,tr[i][0]); text(tr[i][1],48,y+42,0x8fa0b8); y+=68; }
    } else {
        card(28,y,W-56,120,a->sub); text("Встроенный экран-заглушка. Внешние приложения запускаются на весь экран.",48,y+52,0xc8d2e0);
    }
}

static void draw(double t){
    nhits=0;
    if (mode==MODE_BOOT) draw_boot(t-boot_t0);
    else if (mode==MODE_DC) draw_dc(t);
    else if (mode==MODE_APP && open_app>=0) draw_app(t);
    else draw_xmb(t);
    draw_topbar();
    if (now_sec()<toast_until){ int w=text_w(toast_msg)+36; fill_rect(W/2-w/2,58,w,34,0x0b1020); text_c(toast_msg,W/2,80,0xffffff); }
    if (mode!=MODE_BOOT) text("←→↑↓ Enter Esc · F1 Dreamcast/XMB · V голос · геймпад js0",18,H-14,0x9fb2cc);
    XCopyArea(dpy,backbuf,shell_win,gc,0,0,W,H,0,0); XFlush(dpy);
}

/* ───────────────────────── ввод ───────────────────────── */
static void hit_click(int x,int y){
    for(int i=nhits-1;i>=0;i--){ Hit *h=&hits[i]; if(x>=h->x&&x<h->x+h->w&&y>=h->y&&y<h->y+h->h){
        switch(h->action){
            case ACT_CAT: cat_idx=h->arg; item_idx=0; if(mode==MODE_DC) mode=MODE_XMB; break;
            case ACT_ITEM: item_idx=h->arg; open_app_idx(app_index(cat_idx,item_idx)); break;
            case ACT_DC_ORBIT: cat_idx=h->arg; do_select(); break;
            case ACT_SETTING: { static const int hues[6]={222,265,190,330,150,20};
                if(h->arg<6) cfg_hue=hues[h->arg]; else if(h->arg<13) cfg_swirl=h->arg-10;
                else if(h->arg==20) cfg_anim=!cfg_anim; else if(h->arg==21) cfg_fps_battery_save=!cfg_fps_battery_save; else if(h->arg==22) cfg_dc_default=!cfg_dc_default;
                config_save(); toast_set("Настройки сохранены"); break; }
            case ACT_TRACK: music_track = (music_track==h->arg? -1:h->arg); music_t0=now_sec(); break;
            case ACT_STORE: if(store_progress[h->arg]==0) store_progress[h->arg]=1; break;
            case ACT_TILE: if(open_app>=0&&!strcmp(APPS[open_app].title,"Пузыри")){
                    for(int j=0;j<10;j++) if(bubbles[j].alive && hypot(x-bubbles[j].x,y-bubbles[j].y)<bubbles[j].r+8){ bubbles[j].alive=0; bubble_score++;
                        if(bubble_score>=10){ toast_set("10 сфер! Трофей «Ловец сфер»"); voice_feedback("Победа"); bubble_score=0; for(int k=0;k<10;k++){bubbles[k].alive=1; bubbles[k].x=80+rand()%(W-240); bubbles[k].y=140+rand()%(H-300);} }
                        else { bubbles[j].alive=1; bubbles[j].x=80+rand()%(W>240?W-240:200); bubbles[j].y=140+rand()%(H>300?H-300:200); } break; }
                } break;
        } return; } }
    if (mode==MODE_BOOT) mode=cfg_dc_default?MODE_DC:MODE_XMB;
}
static void key_press(XEvent *ev){
    char buf[64]; KeySym ks; XComposeStatus cs; int len=XLookupString(&ev->xkey,buf,sizeof buf,&ks,&cs); (void)len;
    if (mode==MODE_APP && open_app>=0 && !strcmp(APPS[open_app].title,"Заметки") && ks!=XK_Escape){
        if (ks==XK_BackSpace){ size_t n=strlen(notes_buf); if(n) notes_buf[n-1]=0; }
        else if (ks==XK_Return) strncat(notes_buf,"\n",sizeof notes_buf-strlen(notes_buf)-1);
        else if (len>0){ buf[len]=0; strncat(notes_buf,buf,sizeof notes_buf-strlen(notes_buf)-1); }
        notes_save(); return;
    }
    switch(ks){
        case XK_Escape: do_back(); break;
        case XK_Return: case XK_KP_Enter: do_select(); break;
        case XK_Left: do_move(-1,0); break; case XK_Right: do_move(1,0); break;
        case XK_Up: do_move(0,-1); break; case XK_Down: do_move(0,1); break;
        case XK_F1: case XK_Tab: mode = (mode==MODE_DC?MODE_XMB:MODE_DC); break;
        case XK_v: case XK_V: toast_set("Голос: говорите… (демон xross-voiced)"); break;
        default: break;
    }
}
#ifdef __linux__
static int joy_fd=-1;
static void joy_poll(void){
    if (joy_fd<0) return; struct js_event e;
    while (read(joy_fd,&e,sizeof e)==(ssize_t)sizeof e){
        if ((e.type&JS_EVENT_BUTTON)&&e.value){ /* типичный XInput/DualShock порядок */
            if(e.number==0) do_select(); else if(e.number==1) do_back();
            else if(e.number==4) do_move(-1,0); else if(e.number==5) do_move(1,0);
            else if(e.number==9||e.number==16){ if(client_win) close_client(); else mode=cfg_dc_default?MODE_DC:MODE_XMB; } }
        else if ((e.type&JS_EVENT_AXIS)&&abs(e.value)>16000){
            if(e.number==6) do_move(e.value>0?1:-1,0); if(e.number==7) do_move(0,e.value>0?1:-1); }
    }
}
#endif

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

    XSetWindowAttributes attr; attr.override_redirect=True; attr.background_pixel=px(0x05070f);
    shell_win=XCreateWindow(dpy,root_win,0,0,W,H,0,CopyFromParent,InputOutput,CopyFromParent,CWOverrideRedirect|CWBackPixel,&attr);
    XStoreName(dpy,shell_win,"XrossOS");
    XSelectInput(dpy,shell_win,ExposureMask|KeyPressMask|ButtonPressMask|PointerMotionMask|StructureNotifyMask);
    XMapRaised(dpy,shell_win);
    /* WM-функция: перехватываем появление чужих окон ПОСЛЕ показа своего */
    XSelectInput(dpy,root_win,SubstructureRedirectMask|SubstructureNotifyMask);
    /* Ctrl+Esc и Home возвращают из внешнего приложения в оболочку */
    XGrabKey(dpy,XKeysymToKeycode(dpy,XK_Escape),ControlMask,root_win,True,GrabModeAsync,GrabModeAsync);
    XGrabKey(dpy,XKeysymToKeycode(dpy,XK_Home),AnyModifier,root_win,True,GrabModeAsync,GrabModeAsync);

    gc=XCreateGC(dpy,shell_win,0,NULL);
    backbuf=XCreatePixmap(dpy,root_win,W,H,DefaultDepth(dpy,screen_no));
    boot_t0=now_sec();

    mkfifo(VOICE_FIFO_PATH,0666); voice_fd=open(VOICE_FIFO_PATH,O_RDONLY|O_NONBLOCK);
#ifdef __linux__
    joy_fd=open("/dev/input/js0",O_RDONLY|O_NONBLOCK);
#endif
    int xfd=ConnectionNumber(dpy);
    double last_battery_check=0; (void)last_battery_check;

    for(;;){
        double frame_start=now_sec();
        /* события X без busy-loop: select до следующего кадра */
        while (XPending(dpy)){
            XEvent ev; XNextEvent(dpy,&ev);
            switch(ev.type){
                case Expose: break;
                case ConfigureNotify: if(ev.xconfigure.window==shell_win){ W=ev.xconfigure.width; H=ev.xconfigure.height; XFreePixmap(dpy,backbuf); backbuf=XCreatePixmap(dpy,root_win,W,H,DefaultDepth(dpy,screen_no)); if(bg_cache){XFreePixmap(dpy,bg_cache); bg_cache=None;} } break;
                case ButtonPress: hit_click(ev.xbutton.x,ev.xbutton.y); break;
                case KeyPress:
                    if (ev.xkey.state&ControlMask){ KeySym ks=XLookupKeysym(&ev.xkey,0); if(ks==XK_Escape){ close_client(); break; } }
                    if (XLookupKeysym(&ev.xkey,0)==XK_Home && client_win!=None){ close_client(); break; }
                    key_press(&ev); break;
                case MapRequest: { Window w=ev.xmaprequest.window;
                    if (w!=shell_win){ XSelectInput(dpy,w,StructureNotifyMask); XSetWindowBorderWidth(dpy,w,0);
                        XMoveResizeWindow(dpy,w,0,0,W,H); /* ПРИНУДИТЕЛЬНО весь экран */
                        XMapWindow(dpy,w); XSetInputFocus(dpy,w,RevertToParent,CurrentTime); client_win=w; }
                    else XMapWindow(dpy,w);
                    break; }
                case ConfigureRequest: { XWindowChanges wc; wc.x=0; wc.y=0; wc.width=W; wc.height=H; wc.border_width=0;
                    XConfigureWindow(dpy,ev.xconfigurerequest.window,CWX|CWY|CWWidth|CWHeight|CWBorderWidth,&wc); break; }
                case UnmapNotify: case DestroyNotify:
                    if (ev.xany.window==client_win || (ev.type==DestroyNotify && ev.xdestroywindow.window==client_win)){
                        client_win=None; child_pid=-1; XMapRaised(dpy,shell_win); if(mode==MODE_APP) mode=MODE_XMB; }
                    break;
                default: break;
            }
        }
        /* внешний процесс умер, а окна не создал (нет бинарника) — вернуть оболочку */
        if (child_pid>0 && client_win==None){ if (kill(child_pid,0)<0 && errno==ESRCH){ child_pid=-1; XMapRaised(dpy,shell_win); toast_set("Приложение закрылось или не запустилось (см. /var/log/xross.log)"); } }
        voice_poll();
#ifdef __linux__
        joy_poll();
#endif
        /* сглаживание XMB (отключаемо для слабого железа) */
        double k = cfg_anim? 0.22 : 1.0;
        anim_cat += (cat_idx-anim_cat)*k; anim_item += (item_idx-anim_item)*k;
        if (fabs(cat_idx-anim_cat)<0.001) anim_cat=cat_idx; if (fabs(item_idx-anim_item)<0.001) anim_item=item_idx;
        double t=frame_start;
        if (client_win==None) draw(t); /* пока клиент на экране — не тратим CPU Jaguar на фон */
        frame_no++;
        double budget=1.0/target_fps(); double spent=now_sec()-frame_start;
        if (spent<budget){ struct timespec ts={0,(long)((budget-spent)*1e9)}; /* select вместо sleep: не проспим события */
            fd_set rf; FD_ZERO(&rf); FD_SET(xfd,&rf); if(voice_fd>=0) FD_SET(voice_fd,&rf);
            struct timeval tv={0,(long)((budget-spent)*1e6)}; (void)ts; select((xfd>voice_fd?xfd:voice_fd)+1,&rf,NULL,NULL,&tv); }
    }
    return 0;
}
