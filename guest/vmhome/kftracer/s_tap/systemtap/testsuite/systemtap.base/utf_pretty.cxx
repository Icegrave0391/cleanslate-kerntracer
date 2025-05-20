#include <stdint.h>
#include <wchar.h>
#include <uchar.h>
#include <stdlib.h>

static struct {
    char c8;
    char16_t c16;
    char32_t c32;

    const char *s8;
    const char16_t *s16;
    const char32_t *s32;
} strings = {
    's', u't', U'p',
    // various encodings of "stapΑΩ☺😈"
    u8"stap\u0391\u03A9\u263A\U0001F608",
    u"stap\u0391\u03A9\u263A\U0001F608",
    U"stap\u0391\u03A9\u263A\U0001F608",
};

mbstate_t mbs;

int main()
{
    const char16_t* pt = strings.s16;
    char buffer [MB_CUR_MAX];
    size_t length;

    mbrlen (NULL,0,&mbs);   /* initialize mbs */

    while (*pt) {
      length = c16rtomb(buffer,*pt,&mbs);
      if ((length==0)||(length>MB_CUR_MAX)) break;
      ++pt;
    }

 main_return:
    return 0;
}
