#ifndef __ZEBRAENV_H__
#define __ZEBRAENV_H__

#ifndef IPPROTO_OSPFIGP
#define IPPROTO_OSPFIGP		89
#endif

#ifndef ERANGE
#define	ERANGE	34
#endif 

#ifdef _MSC_VER
#include "msvc_env.h"
#else
#include "native_env.h"
#endif

#endif