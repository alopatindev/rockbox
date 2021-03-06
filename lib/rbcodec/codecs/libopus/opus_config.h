#ifndef CONFIG_H
#define CONFIG_H

#include "config.h"
#include "codeclib.h"
#include "ogg/ogg.h"

/* general stuff */
#define OPUS_BUILD

/* alloc stuff */
#define NONTHREADSAFE_PSEUDOSTACK

#define OVERRIDE_OPUS_ALLOC
#define OVERRIDE_OPUS_FREE
#define OVERRIDE_OPUS_ALLOC_SCRATCH

#define opus_alloc          _ogg_malloc
#define opus_free           _ogg_free
#define opus_alloc_scratch  _ogg_malloc

/* lrint */
#define HAVE_LRINTF 0
#define HAVE_LRINT  0

/* embedded stuff */
#define FIXED_POINT
#define DISABLE_FLOAT_API
#define EMBEDDED_ARM 1

/* undefinitions */
#ifdef ABS
#undef ABS
#endif
#ifdef MIN
#undef MIN
#endif
#ifdef MAX
#undef MAX
#endif

#endif /* CONFIG_H */

