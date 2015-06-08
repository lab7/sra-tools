/*===========================================================================
 *
 *                            PUBLIC DOMAIN NOTICE
 *               National Center for Biotechnology Information
 *
 *  This software/database is a "United States Government Work" under the
 *  terms of the United States Copyright Act.  It was written as part of
 *  the author's official duties as a United States Government employee and
 *  thus cannot be copyrighted.  This software/database is freely available
 *  to the public for use. The National Library of Medicine and the U.S.
 *  Government have not placed any restriction on its use or reproduction.
 *
 *  Although all reasonable efforts have been taken to ensure the accuracy
 *  and reliability of the software and data, the NLM and the U.S.
 *  Government do not and cannot warrant the performance or results that
 *  may be obtained by using this software or data. The NLM and the U.S.
 *  Government disclaim all warranties, express or implied, including
 *  warranties of performance, merchantability or fitness for any particular
 *  purpose.
 *
 *  Please cite the author in any work or product based on this material.
 *
 * ===========================================================================
 *
 */
#include <vfs/manager.h>
#include <vfs/path.h>
#include <vfs/manager-priv.h>
#include <vfs/path-priv.h>
#include <kxml/xml.h>
#include <kfs/defs.h>
#include <kfs/directory.h>
#include <kfs/file.h>
#include <kfs/nullfile.h>
#include <kfs/teefile.h>
#include <klib/defs.h>
#include <klib/rc.h>
#include <klib/text.h>
#include <klib/container.h>
#include <klib/vector.h>
#include <klib/out.h>
#include <klib/log.h>
#include <klib/debug.h>
#include <kapp/args.h>
#include "ccextract.vers.h"

#include <string.h>
#include <stdlib.h>
#include <assert.h>

/*
 * some program globals
 */
const char * program_name = "ccextract"; /* default it but try to set it */
const char * full_path = "ccextract"; /* default it but try to set it */


#define OPTION_XML     "copycat-xml"
#define OPTION_FORCE   "force"
#define OPTION_DIR     "directory"

#define ALIAS_XML     "x"
#define ALIAS_FORCE   "f"
#define ALIAS_DIR     "d"


static
const char * xml_usage[] = 
{ "XML generated by 'copycat''", NULL };
static
const char * force_usage[] = 
{ "force overwrite of existing files", NULL };
static
const char * dir_usage[] = 
{ "location of output dbase directory", NULL };


/* Version  EXTERN
 *  return 4-part version code: 0xMMmmrrrr, where
 *      MM = major release
 *      mm = minor release
 *    rrrr = bug-fix release
 */
uint32_t KAppVersion ( void )
{
    return CCEXTRACT_VERS;
}


const char UsageDefaultName [] = "ccextract";


rc_t CC UsageSummary (const char * progname)
{
    return KOutMsg (
        "\n"
        "Usage:\n"
        "  %s [options] [-d|--directory <directory>] -x|--copycat-xml <XML-file>\\\n"
        "          source-archive | [path [...]]"
        "\n"
        "Summary:\n"
        "  Copies files and/or directories, creating a catalog of the copied files.\n",
        progname);
}



/*  ----------------------------------------------------------------------
 */
static
const char * first_usage[] = 
{
    "The path to a archive file ",
};

static
const char * second_usage[] = 
{
    "A file by path or ID to extract",
    "If none are given all files are extracted"
};

rc_t CC Usage (const Args * args)
{
    const char * progname = UsageDefaultName;
    const char * fullpath = UsageDefaultName;
    rc_t rc;

    if (args == NULL)
        rc = RC (rcApp, rcArgv, rcAccessing, rcSelf, rcNull);
    else
        rc = ArgsProgram (args, &fullpath, &progname);

    UsageSummary (progname);

    KOutMsg ("Parameters:\n");

    HelpParamLine ("source-file-path", first_usage);
    HelpParamLine ("extract-path", second_usage);

    KOutMsg ("Options:\n");

    HelpOptionLine (ALIAS_XML, OPTION_XML, "XML-file", xml_usage);
    HelpOptionLine (ALIAS_DIR, OPTION_DIR, "directoy-path", dir_usage);
    HelpOptionLine (ALIAS_FORCE, OPTION_FORCE, NULL, force_usage);

    HelpOptionsStandard ();
/*                     1         2         3         4         5         6         7         8 */
/*            12345678901234567890123456789012345678901234567890123456789012345678901234567890 */
    HelpVersion (fullpath, KAppVersion());

    return rc;
}


/*  ----------------------------------------------------------------------
 */
static
OptDef Options[] = 
{
    /* name            alias max times oparam required fmtfunc help text loc */
    { OPTION_XML,   ALIAS_XML,   NULL, xml_usage,   1, true,  true },
    { OPTION_DIR,   ALIAS_DIR,   NULL, dir_usage,   1, true,  false },
    { OPTION_FORCE, ALIAS_FORCE, NULL, force_usage, 0, false, false }
};


/*  ----------------------------------------------------------------------
 */
static struct
{
    const char       * dirstr;
    VPath            * dirpath;
    KDirectory       * dir;           /* extraction target directory */

    const char       * xmlstr;
    VPath            * xmlpath;
    const KFile      * xml;

    const char       * arcstr;

    /* root directory for XFS is archive as a directory but located below the archive */
    const char       * rootstr;
    const KDirectory * root;

    /* base is the directory made from the archive - xtoc/xfs is a bit incoherent here */
    const char       * basestr;
    VPath            * basepath;
    const KDirectory * base;


    VFSManager * vfsmgr;

    Vector       pathstr;
    Vector       pathvpath;
    BSTree       pathtree;




    KFile *      null;




    bool         force;
    uint32_t     cm;
} options;


/*  ----------------------------------------------------------------------
 */
typedef struct extnode
{
    BSTNode       node;
    const VPath * path;
    uint64_t      offset;
} extnode;


/*  ----------------------------------------------------------------------
 */
static
rc_t extnode_make (extnode ** new_node, const VPath * path, uint64_t offset)
{
    rc_t rc;

    if (new_node == NULL)
    {
        rc = RC (rcExe, rcTree, rcConstructing, rcParam, rcNull);
        LOGERR (klogInt, rc, "missing new_node for making node");
    }
    else
    {
        *new_node = NULL;

        if (path == NULL)
        {
            rc = RC (rcExe, rcTree, rcConstructing, rcParam, rcNull);
            LOGERR (klogInt, rc, "missing path for making node");
        }
        else if (path == NULL)
        {
            rc = RC (rcExe, rcTree, rcConstructing, rcParam, rcNull);
            LOGERR (klogInt, rc, "missing path for making node");
        }
        else
        {
            extnode * node;

            node = malloc (sizeof *node);
            if (node == NULL)
            {
                rc = RC (rcExe, rcTree, rcConstructing, rcMemory, rcExhausted);
                LOGERR (klogFatal, rc, "unable to get memory to make a VPath node");
            }
            else
            {
                rc = VPathAddRef (path);
                if (rc == 0)
                {
                    node->path = path;
                    node->offset = offset;
                    *new_node = node;
                    return 0;
                }
                free (node);
            }
        }
    }
    return rc;
}


/*  ----------------------------------------------------------------------
 */
static
void CC extnode_whack (BSTNode * n, void * data)
{
    if (n)
    {
        VPathRelease (((extnode*)n)->path);
        free (n);
    }
}


/*  ----------------------------------------------------------------------
 */
static
int64_t CC extnode_sort (const BSTNode * item, const BSTNode * n)
{
    const extnode * l;
    const extnode * r;
    int64_t       ii;

    l = (const extnode *)item;
    r = (const extnode *)n;

    if (l->offset < r->offset)
        ii = -1;

    else if (l->offset > r->offset)
        ii = 1;

    else
    {
        size_t lz;
        size_t rz;
        char lbuff [8192];
        char rbuff [8192];
        rc_t lrc;
        rc_t rrc;

        lrc = VPathReadPath (l->path, lbuff, sizeof (lbuff), &lz);
        if (lrc)
        {
            LOGERR (klogInt, lrc, "failed to etract item path");
            lz = 0;
        }

        rrc = VPathReadPath (r->path, rbuff, sizeof (rbuff), &rz);
        if (lrc)
        {
            LOGERR (klogInt, rrc, "failed to etract node path");
            rz = 0;
        }

        ii = string_cmp (lbuff, lz, rbuff, rz, lz + rz);
    }
    return ii;
}


/*  ----------------------------------------------------------------------
 */
typedef struct rc_data
{
    rc_t rc;
} rc_data;


/*  ----------------------------------------------------------------------
 */
static
bool CC extract_one (BSTNode * n, void * data_)
{
    extnode * node;
    rc_data * data = data_;
    rc_t rc;
    size_t z;
    char buff [8193];

    assert (n);
    assert (data);

    node = (extnode*)n;

    rc = VPathReadPath (node->path, buff, sizeof (buff) - 1, &z);
    if (rc)
        LOGERR (klogErr, rc, "error pulling path for an extraction");
    else
    {
        const KFile * sfile;

        buff[z] = '\0';

/*
 * use base unless we have to revert to root.
 * base allows more control over options like password where the outside
 * archive might have a different password than an inner file
 */
#if 1 
        rc = VFSManagerOpenFileReadDirectoryRelative (options.vfsmgr, options.base,
                                                          &sfile, node->path);
#else
        rc = VFSManagerOpenFileReadDirectoryRelative (options.vfsmgr, options.root,
                                                      &sfile, node->path);
#endif
        if (rc)
            LOGERR (klogErr, rc, "error opening file within the archive");
        else
        {
            KFile * dfile;

/*             KOutMsg ("%s: %s %x\n", __func__, node->path, options.cm); */
            rc = KDirectoryCreateFile (options.dir, &dfile, false, 0640, options.cm, "%s", buff);
            if (rc)
                PLOGERR (klogErr, (klogErr, rc, "failed to create file '$(P)'", "P=%s", buff));
            else
            {
                const KFile * teefile;

                rc = KFileMakeTeeRead (&teefile, sfile, dfile);
                if (rc)
                    PLOGERR (klogErr, (klogErr, rc, "failed pipefitting file '$(P)'", "P=%s", buff));
                else
                {
                    KFileAddRef (sfile);
                    KFileAddRef (dfile);
                    rc = KFileRelease (teefile);
                    if (rc)
                    PLOGERR (klogErr, (klogErr, rc, "failed copying file '$(P)'", "P=%s", buff));
                }
            }
            KFileRelease (sfile);
        }
        KFileRelease (sfile);
    }
    data->rc = rc;
    return (rc != 0);
}


/*  ----------------------------------------------------------------------
 */
static
rc_t extract ()
{
    rc_data data;
    bool failed;

    /* done sequentially - this will cause back ups on reads if both
     * containers and their contents are extracted
     *
     * we are also using a DoUntil approach that quits at the first failed
     * extract
     */
    failed = BSTreeDoUntil (&options.pathtree, false, extract_one, &data);
    
    if (failed)
        LOGERR (klogErr, data.rc, "failure extracting a file");

    return data.rc;
}


/*  ----------------------------------------------------------------------
 */
#if 0
static
int CC sort_pathpath_cmp (const void ** litem, const void ** ritem, void * data)
{
    uint64_t lloc, rloc;

    {
        const VPath * lpath;
        size_t z;
        rc_t rc;
        char pbuff [8192];

        lpath = *litem;

        rc = VPathReadPath (lpath, pbuff, sizeof pbuff, &z);
        if (rc == 0)
        {
            switch (KDirectoryPathType (options.base, "%s", pbuff))
            {
            default:
                lloc = 0;
                break;
            case kptFile:
                rc = KDirectoryFileLocator (options.base, &lloc, "%s", pbuff);
                break;
            }
            if (rc == 0)
            {
                const VPath * rpath;

                rpath = *ritem;
                rc = VPathReadPath (rpath, pbuff, sizeof pbuff, &z);
                if (rc == 0)
                {
                    switch (KDirectoryPathType (options.base, "%s", pbuff))
                    {
                    default:
                        rloc = 0;
                        break;
                    case kptFile:
                        rc = KDirectoryFileLocator (options.base, &rloc, "%s", pbuff);
                        break;
                    }
                }
            }
        }
        if (rc) /* surrender */
            lloc = rloc = 0;
    }
    
    {
        int cmp;

        if (lloc < rloc)
            cmp = -1;
        else if (lloc > rloc)
            cmp = 1;
        else
        {
            assert (lloc == rloc);
            cmp = 0;
        };

        return cmp;
    }
}
#endif


#if 0
/*  ----------------------------------------------------------------------
 */
static
bool CC build_tree_add (void * _item, void * _data)
{
    build_tree_data * data = _data;
    return false;
}


/*  ----------------------------------------------------------------------
 */
static
rc_t build_tree ()
{
    
    rc_t rc;
    rc_data data;
    bool did_until = false;

    BSTreeInit (&options.pathtree);

    VectorDoUntil (options.pathpath, false, build_tree_add, &data);
}
#endif

/*  ----------------------------------------------------------------------
 */
static
rc_t insert_path (const VPath * vpath, uint64_t offset)
{
    extnode * node;
    rc_t rc;

    rc = extnode_make (&node, vpath, offset);
    if (rc == 0)
    {
        rc = BSTreeInsert (&options.pathtree, &node->node, extnode_sort);
        if (rc)
            LOGERR (klogInt, rc, "error inserting tree node");
        else
            return 0;

        extnode_whack (&node->node, NULL);
    }
    return rc;
}


/* ----------------------------------------------------------------------
 */
static
rc_t walk_path_file (char * path, size_t z, uint64_t * offset, KPathType kpt)
{
    uint64_t this_offset;
    char * pc;
    rc_t rc;

    assert (path);
    assert (offset);

    this_offset = 0;
    rc = 0;
    if (kpt == kptFile)
    {
        rc = KDirectoryFileLocator (options.base, &this_offset, "%s", path);
        if (rc)
            PLOGERR (klogErr,
                     (klogErr, rc, "failure walking path '$(P)'",
                      "P=%s", path));
    }
    if (rc == 0)
    {
        pc = string_rchr (path, z, '/');
        if (pc)
        {
            uint64_t that_offset;
            KPathType lkpt;

            *pc = '\0';
            lkpt = KDirectoryPathType (options.base, "%s", path);

            switch (lkpt)
            {
            default:
                rc = RC (rcExe, rcPath, rcAccessing, rcPath, rcInvalid);
                break;
            case kptNotFound:
            case kptZombieFile:
                rc = RC (rcExe, rcPath, rcAccessing, rcPath, rcNotFound);
                break;

            case kptBadPath:
                rc = RC (rcExe, rcPath, rcAccessing, rcPath, rcInvalid);
                break;

            case kptFile:
            case kptDir:
                /* we should always hit here */
                rc = walk_path_file (path, z, &that_offset, lkpt);
                if (rc == 0)
                {
                    this_offset += that_offset;
                }
                break;

            case kptCharDev:
            case kptBlockDev:
            case kptFIFO:
                rc = RC (rcExe, rcPath, rcAccessing, rcPath, rcIncorrect);
                break;
            }
            *pc = '/';
        }
    }
    *offset = this_offset;
    return rc;
}


/*  ----------------------------------------------------------------------
 */
static
rc_t walk_path_tree (char * path, size_t z)
{
    return 0;
}


/*  ----------------------------------------------------------------------
 * signature is because this is called by VectorDoUntil
 */
static
void CC handle_path (void * item_, void * data_)
{
    if ((item_ != NULL) && (data_ != NULL))
    {
        VPath * vpath = item_;
        rc_data * data = data_;
        size_t z;
        rc_t rc;
        char pbuff [8193];
/*         char tbuff [8193]; */

        if (data->rc)
            return;

        /* -1 saves room for a guaranteed NUL */
        rc = VPathReadPath (vpath, pbuff, sizeof (pbuff) - 1, &z);
        if (rc)
            LOGERR (klogErr, rc, "failed to pull path out of VPath");

        else if (z)
        {
            KPathType kpt;

            /* force a NUL just in case: we saved room for it */
            pbuff[z] = '\0';

            /* what type of path is this? */
            kpt = KDirectoryPathType (options.base, "%s", pbuff);

#if 0
/* ain't doing this now, and may never because of the root versus base problems */
            /* dereference links until we're done */
            while (kpt & kptAlias)
            {
                rc = KDirectoryResolveAlias (options.base, true,
                                             tbuff, sizeof tbuff, "%s", pbuff);
                if (rc)
                {
                    PLOGERR (klogErr,
                             (klogErr, rc, "error resolving path '$(P)'",
                              "P=%s", pbuff));
                    break;
                }
                else
                {
                    memcpy (tbuff, pbuff, sizeof pbuff);
                    z = string_size (pbuff);
                    kpt = KDirectoryPathType (options.root, "%s", pbuff);
                }
            }
#endif
            /* if we didn't crash this path dereferecing it. */
            if (rc == 0)
            {
                if (kpt & kptAlias)
                    kpt = kptAlias;

                switch (kpt)
                {
                default:
                    rc = RC (rcExe, rcPath, rcAccessing, rcPath, rcInvalid);
                    PLOGERR (klogErr,
                             (klogErr, rc, "unknown problem with path '$(P)'",
                              "P=%s", pbuff));
                    break;
                case kptNotFound:
                    PLOGERR (klogErr,
                             (klogErr, rc, "path is a not found in archive '$(P)'",
                              "P=%s", pbuff));
                    break;

                case kptZombieFile:
                    rc = RC (rcExe, rcPath, rcAccessing, rcPath, rcNotFound);
                    PLOGERR (klogErr,
                             (klogErr, rc, "path is a not in archive but should be '$(P)'",
                              "P=%s", pbuff));
                    break;

                case kptBadPath:
                    rc = RC (rcExe, rcPath, rcAccessing, rcPath, rcInvalid);
                    PLOGERR (klogErr,
                             (klogErr, rc, "unusable path form '$(P)'",
                              "P=%s", pbuff));
                    break;

                case kptFile:
                {
                    uint64_t offset = 0;
                    rc = walk_path_file (pbuff, z, &offset, kptFile);
                    if (rc)
                        PLOGERR (klogErr,
                                 (klogErr, rc, "couldn't walk path '$(P)'",
                                  "P=%s", pbuff));
                    else
                    {
                        rc = insert_path (vpath, offset);
                        if (rc)
                            PLOGERR (klogErr,
                                     (klogErr, rc, "couldn't sort path '$(P)'",
                                      "P=%s", pbuff));
                    }
                    break;
                }
                case kptDir:
                    rc = walk_path_tree (pbuff, z);
                    break;

                case kptCharDev:
                case kptBlockDev:
                case kptFIFO:
                case kptAlias:
                    rc = RC (rcExe, rcPath, rcAccessing, rcPath, rcIncorrect);
                    PLOGERR (klogErr,
                             (klogErr, rc, "unusable path target type '$(P)'",
                              "P=%s", pbuff));
                    break;
                }
            }
        }
        data->rc = rc;
    }
}


/*  ----------------------------------------------------------------------
 */
static
rc_t build_tree_then_run ()
{
    rc_data data;

    data.rc = 0;

    BSTreeInit (&options.pathtree);

    VectorForEach (&options.pathvpath, false, handle_path, &data);

    if (data.rc == 0)
        data.rc = extract();

    BSTreeWhack (&options.pathtree, extnode_whack, NULL);
    
    return data.rc;
}

/*  ----------------------------------------------------------------------
 */
static
void CC build_vpath_one (void * item, void * data)
{
    const char * path;
    rc_data * prc;

    path = item;
    prc = data;

    if (prc->rc == 0)
    {
        static const char ccid [] = "copycat-id:";
        const size_t cz = sizeof (ccid) - 1;
        size_t pz;
        const char * hier;
        const char * ppath;
        VPath * vpath;
        rc_t rc;
        char vbuff [8193];

        rc = 0;
        ppath = path;
        pz = string_size (path);
        hier = string_chr (path, pz, ':');

        if ((hier != NULL) &&
            (0 == string_cmp (path, (hier+1) - path, ccid, cz /*sizeof (ccid) - 1*/, cz)))
        {
            static const char nfile[] = "ncbi-file:";
            char * qmark;
            size_t s, r/*, q */;
            char ibuff [8192];
            char rbuff [8192];

            ++hier;
            s = hier - path;
            r = string_copy (ibuff, sizeof (ibuff), hier, pz - s);

            qmark = string_chr (ibuff, r, '?');
            if (qmark == NULL) /* this is more future with parts */
                qmark = string_chr (ibuff, r, '#');

            if (qmark != NULL)
                *qmark = '\0';

            rc = KDirectoryResolveAlias (options.root, true, rbuff, sizeof (rbuff), "%s", ibuff);

            if (rc)
                PLOGERR (klogErr, (klogErr, rc, "error resolving file id '$(I)", "I=%s", ibuff));

            else 
            {
                char * slash;
                size_t z;

                slash = string_chr (rbuff+1, sizeof (rbuff), '/');
                if (slash == NULL)
                    /* we won't extract the root */
                    return;

                ++slash;

                z = string_size (slash);
                if (z == 0)
                    return;

                if (qmark)
                {
                    s = string_copy (vbuff, sizeof (vbuff), nfile, sizeof (nfile));
                    r = string_copy (vbuff + s, sizeof (vbuff) - s, slash, z);
                    /*q = */string_copy (vbuff + s + r, (sizeof (vbuff) - s) - r, qmark, pz - (qmark - path));
                }
                else
                {
                    s = string_copy (vbuff, sizeof (vbuff), slash, z);
                }
                ppath = vbuff;
            }
        }

        if (rc == 0)
        {
            rc = VFSManagerMakePath (options.vfsmgr, &vpath, "%s", ppath);
            if (rc)
                ;
            else
            {
                rc = VectorAppend (&options.pathvpath, NULL, vpath);
                if (rc)
                {
                    VPathRelease (vpath);
                }
            }
        }
        
        prc->rc = rc;
    }
}


/*  ----------------------------------------------------------------------
 */
static
void CC build_vpath_whack (void * item, void * data)
{
    VPath * p;

    p = item;

    VPathRelease ( p );
}

/*  ----------------------------------------------------------------------
 * pull paramstring 1-N and comnvert then to internal VPaths
 */
static
rc_t build_vpath_then_run ()
{
    rc_data data;

    data.rc = 0;

    VectorInit (&options.pathvpath, 0, VectorLength (&options.pathstr));

    VectorForEach (&options.pathstr, false, build_vpath_one, &data);

    if (data.rc == 0)
        build_tree_then_run();

    VectorWhack (&options.pathvpath, build_vpath_whack, NULL);

    return data.rc;
}


/*  ----------------------------------------------------------------------
 * SCHEME:PATH/FILE?QUERY
 *
 * form_one is just a file
 * form_two is just a path and a file (can ignore scheme until more schemes supported)
 * form_three is all parts except path present which for here acts like form_one
 * form_four is all four parts
 *
 * path is the directory path leading to root
 * root will be the directory containing the archive
 * base will be the archive as a directory
 */
static
rc_t open_root_then_run ()
{
    static const char dot[] = ".";
    char         rootstr [8192];
    char         basestr [8192];
    const char * colon;
    rc_t rc;

    colon = strchr (options.arcstr, ':');
    if (colon == NULL) /* no scheme so it has to be a plain path */
    {
        char * last_slash;

        strcpy (basestr, options.arcstr);
        last_slash = strrchr (basestr, '/');

        if (last_slash == NULL) /* in this directory */
        {
            options.rootstr = dot;
            options.basestr = options.arcstr;
            /* done */
        }
        else
        {
            *last_slash = '\0';
            options.rootstr = basestr;
            options.basestr = last_slash + 1;
            /* done */
        }
    }
    else
    {
        char * end_of_root;
        char * last_slash;

        strcpy (rootstr, colon+1);

        end_of_root = strchr (rootstr, '?');

        if (end_of_root == NULL)
            end_of_root = strchr (rootstr, '#');

        if (end_of_root)
            *end_of_root = '\0';

        options.rootstr = rootstr;

        last_slash = strchr (rootstr, '/');
        if (last_slash == NULL)
        {
            /* no path */
            options.rootstr = dot;
            options.basestr = options.arcstr;
            /* done */
        }
        else
        {
            size_t x,z;

            *last_slash = '\0';
            options.rootstr = rootstr;

            /* scheme */
            z = string_size (rootstr);

            x = string_copy (basestr, sizeof (basestr), options.arcstr, (colon + 1) - options.arcstr);
            strcpy (basestr + x, options.arcstr + x + z + 1);
            options.basestr = basestr;
            /* done */
        }
    }
    {
        KDirectory * cwd;

        rc = VFSManagerGetCWD (options.vfsmgr, &cwd);
        if (rc)
            ;
        else
        {
            rc = KDirectoryOpenXTocDirRead (cwd, &options.root, true,
                                            options.xml, "%s", options.rootstr);
            if (rc)
                PLOGERR (klogErr, (klogErr, rc,
                                   "failed to open XFS from '$(P)' using '$(P)'",
                                   "P=%s", options.basestr, options.xmlstr));
            else
            {
                rc = VFSManagerMakePath (options.vfsmgr, &options.basepath, "%s", options.basestr);
                if (rc)
                    PLOGERR (klogErr, (klogErr, rc,
                                       "failed to make vpath from '$(P)'",
                                       "P=%s", options.basestr));
                else
                {
                    rc = VFSManagerOpenDirectoryRead (options.vfsmgr,
                                                      &options.base,
                                                      options.basepath);
                    if (rc == 0)
                    {
                        rc = build_vpath_then_run ();

                        KDirectoryRelease (options.base);
                    }
                    KDirectoryRelease (options.root);
                }
                VPathRelease (options.basepath);
            }
            KDirectoryRelease (cwd);
        }
    }
    return rc;
}


/*  ----------------------------------------------------------------------
 */
static
rc_t open_xml_then_run()
{
    rc_t rc;

    rc = VFSManagerMakePath (options.vfsmgr, &options.xmlpath, "%s", options.xmlstr);
    if (rc)
        PLOGERR (klogInt,
                 (klogInt, rc, "failed to create path for '$(P)'",
                  "P=%s", options.xmlstr));
    else
    {
        rc = VFSManagerOpenFileRead (options.vfsmgr, &options.xml, options.xmlpath);
        if (rc)
            LOGERR (klogErr, rc, "Failed to open output directoryCopycat XML file");
        else
        {
            rc = open_root_then_run ();
        }
        VPathRelease (options.xmlpath);
    }
    return rc;
}


/*  ----------------------------------------------------------------------
 */
static
rc_t open_dir_then_run()
{
    rc_t rc;

    rc = VFSManagerMakePath (options.vfsmgr, &options.dirpath, "%s", options.dirstr);
    if (rc)
        PLOGERR (klogInt,
                 (klogInt, rc, "failed to create path for '$(P)'",
                  "P=%s", options.dirstr));
    else
    {
        rc = VFSManagerOpenDirectoryUpdate (options.vfsmgr, &options.dir, options.dirpath);
        if (rc)
            LOGERR (klogErr, rc, "Failed to open output directory");
        else
        {
            rc = open_xml_then_run();
            KDirectoryRelease (options.dir);
        }
        VPathRelease (options.dirpath);
    }
    return rc;
}


/*  ----------------------------------------------------------------------
 */
static
rc_t open_mgr_then_run()
{
    rc_t rc;

    rc = VFSManagerMake (&options.vfsmgr);
    if (rc)
        LOGERR (klogInt, rc, "failed to create VFS manager");
    else
    {
        rc = open_dir_then_run ();
    }
    return rc;
}


/*  ----------------------------------------------------------------------
 * KMain
 *
 * Figure out what is on the command line
 */
rc_t KMain ( int argc, char *argv [] )
{
    Args * args;
    rc_t rc;

    rc = ArgsMakeAndHandle (&args, argc, argv, 1, Options, sizeof Options / sizeof (OptDef));
    if (rc == 0)
    {
        /* use do {} while; for easy outs */
        do
        {
            const char * pc;
            uint32_t pcount;

            rc = ArgsProgram (args, &full_path, &program_name);
            if (rc)
            {
                PLOGERR (klogFatal,
                         (klogFatal, rc,  "failed to set name to $'(N)'",
                          "N=%s", program_name));
                break;
            }

            rc = ArgsOptionCount (args, OPTION_FORCE, &pcount);
            if (rc)
            {
                LOGERR (klogFatal, rc, "failed to check force option");
                break;
            }
            if (pcount)
            {
                options.force = true;
                options.cm = kcmParents | kcmInit;
            }
            else
            {
                options.force = true;
                options.cm = kcmParents | kcmCreate;
            }

            rc = ArgsOptionCount (args, OPTION_XML, &pcount);
            if (rc)
            {
                LOGERR (klogFatal, rc, "failed to check XML option");
                break;
            }
            if (pcount)
            {
                rc = ArgsOptionValue (args, OPTION_XML, 0, &options.xmlstr);
                if (rc)
                {
                    LOGERR (klogFatal, rc, "failed to get XML value");
                    break;
                }
            }
            else
            {
                rc = RC (rcExe, rcArgv, rcParsing, rcParam, rcNull);
                LOGERR (klogFatal, rc, "missing required copycat-xml option");
                MiniUsage(args);
                break;
            }

            rc = ArgsOptionCount (args, OPTION_DIR, &pcount);
            if (rc)
            {
                LOGERR (klogFatal, rc, "failed to check directory option");
                break;
            }
            if (pcount)
            {
                rc = ArgsOptionValue (args, OPTION_DIR, 0, &options.dirstr);
                if (rc)
                {
                    LOGERR (klogFatal, rc, "failed to get directory value");
                    break;
                }
            }
            else
            {
                options.dirstr = ".";
            }


            rc = ArgsParamCount (args, &pcount);
            if (rc)
            {
                LOGERR (klogFatal, rc, "failed to count parameters");
                break;
            }
            if (pcount == 0)
            {
                rc = RC ( rcExe, rcArgv, rcReading, rcParam, rcInsufficient );
                LOGERR (klogFatal, rc, "Missing archive parameter");
                MiniUsage (args);
                break;
            }

            {
                uint32_t block;

                block = 1;
                if (pcount > 2)
                    block = pcount - 1;

                VectorInit (&options.pathstr, 0, block);
            }

            rc = ArgsParamValue (args, 0, &options.arcstr);
            if (rc)
                LOGERR (klogFatal, rc, "failed to retrieve archive parameter");
            else
            {
                if (pcount == 1)
                {
                    rc = VectorAppend (&options.pathstr, NULL, ".");
                    if (rc)
                        LOGERR (klogFatal, rc, "failed to set default path parameter");
                }
                else
                {
                    uint32_t ix;

                    for (ix = 1; ix < pcount; ++ix)
                    {
                        rc = ArgsParamValue (args, ix, &pc);
                        if (rc)
                        {
                            PLOGERR (klogFatal, 
                                     (klogFatal, rc, "unable to extract path parameter '$(K)",
                                      "K=%u", ix));
                            break;
                        }
                        rc = VectorAppend (&options.pathstr, NULL, pc);
                        if (rc)
                        {
                            PLOGERR (klogFatal, 
                                     (klogFatal, rc, "failed to add path '$(P)' to list",
                                      "P=%s", pc));
                            break;
                        }
                    }
                }
            }
            if (rc == 0)
                open_mgr_then_run();

            VectorWhack (&options.pathstr, NULL, NULL);

        } while (0);
        ArgsWhack (args);
    }                
    return rc;
}
