#include <iconv.h>

iconv_t libiconv_open(const char *tocode, const char *fromcode) {
  return iconv_open(tocode, fromcode);
}

size_t libiconv(iconv_t cd, char **inbuf, size_t *inbytesleft, char **outbuf,
                size_t *outbytesleft) {
  return iconv(cd, inbuf, inbytesleft, outbuf, outbytesleft);
}

int libiconv_close(iconv_t cd) { return iconv_close(cd); }
