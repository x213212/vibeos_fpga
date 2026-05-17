#include <string.h>
#include <stdlib.h>
#include <parserutils/charset/codec.h>
#include "charset/aliases.h"
#include "charset/codecs/codec_impl.h"

extern parserutils_charset_handler charset_utf8_codec_handler;

parserutils_error parserutils_charset_codec_create(const char *charset,
		parserutils_charset_codec **codec)
{
	parserutils_charset_codec *c;
	parserutils_error error;
	if (codec == NULL) return PARSERUTILS_BADPARM;
	error = charset_utf8_codec_handler.create("UTF-8", &c);
	if (error != PARSERUTILS_OK) return error;
	c->mibenum = 106;
	c->errormode = PARSERUTILS_CHARSET_CODEC_ERROR_LOOSE;
	*codec = c;
	return PARSERUTILS_OK;
}

parserutils_error parserutils_charset_codec_destroy(parserutils_charset_codec *codec)
{
	if (codec == NULL) return PARSERUTILS_BADPARM;
	codec->handler.destroy(codec);
	free(codec);
	return PARSERUTILS_OK;
}

parserutils_error parserutils_charset_codec_setopt(parserutils_charset_codec *codec,
		parserutils_charset_codec_opttype opt,
		parserutils_charset_codec_optparams *params)
{
	return PARSERUTILS_OK;
}

parserutils_error parserutils_charset_codec_decode(
		parserutils_charset_codec *codec,
		const uint8_t **source, size_t *sourcelen,
		uint8_t **dest, size_t *destlen)
{
	return codec->handler.decode(codec, source, sourcelen, dest, destlen);
}

parserutils_error parserutils_charset_codec_encode(
		parserutils_charset_codec *codec,
		const uint8_t **source, size_t *sourcelen,
		uint8_t **dest, size_t *destlen)
{
	return codec->handler.encode(codec, source, sourcelen, dest, destlen);
}

parserutils_error parserutils_charset_codec_reset(parserutils_charset_codec *codec)
{
	return codec->handler.reset(codec);
}

parserutils_error parserutils_charset_codec_null_codec(parserutils_charset_codec **codec)
{
	return parserutils_charset_codec_create("UTF-8", codec);
}
