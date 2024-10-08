/*
 * Copyright (c) 2022 Winsider Seminars & Solutions, Inc.  All rights reserved.
 *
 * This file is part of System Informer.
 *
 * Authors:
 *
 *     wj32    2010-2016
 *     dmex    2016-2024
 *
 */

#ifndef PH_PHAPPRES_H
#define PH_PHAPPRES_H

#ifndef PHAPP_VERSION_MAJOR
#define PHAPP_VERSION_MAJOR 0
#endif

#ifndef PHAPP_VERSION_MINOR
#define PHAPP_VERSION_MINOR 0
#endif

#ifndef PHAPP_VERSION_BUILD
#define PHAPP_VERSION_BUILD 0
#endif

#ifndef PHAPP_VERSION_REVISION
#define PHAPP_VERSION_REVISION 0
#endif

#ifndef PHAPP_VERSION_COMMITHASH
#define PHAPP_VERSION_COMMITHASH ""
#endif

#define DO_MAKE_STR(x) #x
#define MAKE_STR(x)    DO_MAKE_STR(x)

#define PHAPP_VERSION_STRING MAKE_STR(PHAPP_VERSION_MAJOR) "." MAKE_STR(PHAPP_VERSION_MINOR) "." MAKE_STR(PHAPP_VERSION_BUILD) "." MAKE_STR(PHAPP_VERSION_REVISION)
#define PHAPP_VERSION_NUMBER PHAPP_VERSION_MAJOR,PHAPP_VERSION_MINOR,PHAPP_VERSION_BUILD,PHAPP_VERSION_REVISION
#define PHAPP_VERSION_COMMIT MAKE_STR(PHAPP_VERSION_COMMITHASH)

#endif // PHAPPRES_H
