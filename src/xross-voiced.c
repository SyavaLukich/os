/* xross-voiced — голосовой демон XrossOS (C, офлайн Vosk).
 * Читает PCM 16kHz/16bit/mono из stdin (arecord), распознаёт через
 * libvosk (dlopen — оболочка от vosk не зависит), фразы в нижнем
 * регистре печатает по одной на строку в stdout.
 * Сессия: arecord -q -r16000 -f S16_LE -c1 | xross-voiced модель > /tmp/xross-voice.fifo
 * Сборка: make voice  (нужен только -ldl, vosk_api подгружается в рантайме). */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct VoskModel VoskModel; typedef struct VoskRecognizer VoskRecognizer;
static VoskModel *(*p_model_new)(const char *);
static VoskRecognizer *(*p_rec_new)(VoskModel *, float);
static int (*p_rec_accept)(VoskRecognizer *, const char *, int);
static const char *(*p_rec_final)(VoskRecognizer *);
static const char *(*p_rec_result)(VoskRecognizer *);

static const char *json_text(const char *js) {
    static char out[1024];
    const char *k = strstr(js, "\"text\""); if (!k) return "";
    k = strchr(k + 6, ':'); if (!k) return "";
    k = strchr(k, '"'); if (!k) return "";
    size_t n = 0; for (k++; *k && *k != '"' && n < sizeof out - 1; k++) out[n++] = *k;
    out[n] = 0; return out;
}

int main(int argc, char **argv) {
    const char *model_path = argc > 1 ? argv[1] : "/usr/share/vosk/model-ru";
    void *lib = dlopen("libvosk.so", RTLD_NOW);
    if (!lib) lib = dlopen("/usr/lib/libvosk.so", RTLD_NOW);
    if (!lib) { fprintf(stderr, "xross-voiced: libvosk.so не найдена (пакет vosk в образе ОС)\n"); return 1; }
    p_model_new = (void *)dlsym(lib, "vosk_model_new");
    p_rec_new = (void *)dlsym(lib, "vosk_recognizer_new");
    p_rec_accept = (void *)dlsym(lib, "vosk_recognizer_accept_waveform");
    p_rec_final = (void *)dlsym(lib, "vosk_recognizer_final_result");
    p_rec_result = (void *)dlsym(lib, "vosk_recognizer_result");
    if (!p_model_new || !p_rec_new || !p_rec_accept || !p_rec_final) { fprintf(stderr, "xross-voiced: неполный API vosk\n"); return 1; }
    VoskModel *model = p_model_new(model_path);
    if (!model) { fprintf(stderr, "xross-voiced: нет модели %s (vosk-model-small-ru)\n", model_path); return 1; }
    VoskRecognizer *rec = p_rec_new(model, 16000.0f);
    char pcm[8000];
    size_t n;
    setvbuf(stdout, NULL, _IOLBF, 0);
    while ((n = fread(pcm, 1, sizeof pcm, stdin)) > 0) {
        if (p_rec_accept(rec, pcm, (int)n)) {
            const char *txt = json_text(p_rec_result ? p_rec_result(rec) : p_rec_final(rec));
            if (txt[0]) puts(txt);
        }
    }
    const char *txt = json_text(p_rec_final(rec));
    if (txt[0]) puts(txt);
    return 0;
}
