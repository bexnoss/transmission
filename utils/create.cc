/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <array>
#include <stdio.h> /* fprintf() */
#include <stdlib.h> /* strtoul(), EXIT_FAILURE */
#include <inttypes.h> /* PRIu32 */

#include <libtransmission/transmission.h>
#include <libtransmission/error.h>
#include <libtransmission/file.h>
#include <libtransmission/makemeta.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "units.h"

using namespace std::literals;

static char constexpr MyName[] = "transmission-create";
static char constexpr Usage[] = "Usage: transmission-create [options] <file|directory>";

static uint32_t constexpr KiB = 1024;

static auto constexpr Options = std::array<tr_option, 8>{
    { { 'p', "private", "Allow this torrent to only be used with the specified tracker(s)", "p", false, nullptr },
      { 'r', "source", "Set the source for private trackers", "r", true, "<source>" },
      { 'o', "outfile", "Save the generated .torrent to this filename", "o", true, "<file>" },
      { 's', "piecesize", "Set the piece size in KiB, overriding the preferred default", "s", true, "<KiB>" },
      { 'c', "comment", "Add a comment", "c", true, "<comment>" },
      { 't', "tracker", "Add a tracker's announce URL", "t", true, "<url>" },
      { 'V', "version", "Show version number and exit", "V", false, nullptr },
      { 0, nullptr, nullptr, nullptr, false, nullptr } }
};

struct app_options
{
    std::vector<tr_tracker_info> trackers;
    bool is_private = false;
    bool show_version = false;
    char const* comment = nullptr;
    char const* outfile = nullptr;
    char const* infile = nullptr;
    uint32_t piecesize_kib = 0;
    char const* source = nullptr;
};

static int parseCommandLine(app_options& options, int argc, char const* const* argv)
{
    int c;
    char const* optarg;

    while ((c = tr_getopt(Usage, argc, argv, std::data(Options), &optarg)) != TR_OPT_DONE)
    {
        switch (c)
        {
        case 'V':
            options.show_version = true;
            break;

        case 'p':
            options.is_private = true;
            break;

        case 'o':
            options.outfile = optarg;
            break;

        case 'c':
            options.comment = optarg;
            break;

        case 't':
            options.trackers.push_back(tr_tracker_info{ 0, const_cast<char*>(optarg), nullptr, 0 });
            break;

        case 's':
            if (optarg != nullptr)
            {
                char* endptr = nullptr;
                options.piecesize_kib = strtoul(optarg, &endptr, 10);

                if (endptr != nullptr && *endptr == 'M')
                {
                    options.piecesize_kib *= KiB;
                }
            }

            break;

        case 'r':
            options.source = optarg;
            break;

        case TR_OPT_UNK:
            options.infile = optarg;
            break;

        default:
            return 1;
        }
    }

    return 0;
}

static char* tr_getcwd(void)
{
    char* result;
    tr_error* error = nullptr;

    result = tr_sys_dir_get_current(&error);

    if (result == nullptr)
    {
        fprintf(stderr, "getcwd error: \"%s\"", error->message);
        tr_error_free(error);
        result = tr_strdup("");
    }

    return result;
}

int tr_main(int argc, char* argv[])
{
    char* out2 = nullptr;
    tr_metainfo_builder* b = nullptr;

    tr_logSetLevel(TR_LOG_ERROR);
    tr_formatter_mem_init(MEM_K, MEM_K_STR, MEM_M_STR, MEM_G_STR, MEM_T_STR);
    tr_formatter_size_init(DISK_K, DISK_K_STR, DISK_M_STR, DISK_G_STR, DISK_T_STR);
    tr_formatter_speed_init(SPEED_K, SPEED_K_STR, SPEED_M_STR, SPEED_G_STR, SPEED_T_STR);

    auto options = app_options{};
    if (parseCommandLine(options, argc, (char const* const*)argv) != 0)
    {
        return EXIT_FAILURE;
    }

    if (options.show_version)
    {
        fprintf(stderr, "%s %s\n", MyName, LONG_VERSION_STRING);
        return EXIT_SUCCESS;
    }

    if (options.infile == nullptr)
    {
        fprintf(stderr, "ERROR: No input file or directory specified.\n");
        tr_getopt_usage(MyName, Usage, std::data(Options));
        fprintf(stderr, "\n");
        return EXIT_FAILURE;
    }

    if (options.outfile == nullptr)
    {
        tr_error* error = nullptr;
        char* base = tr_sys_path_basename(options.infile, &error);

        if (base == nullptr)
        {
            fprintf(stderr, "ERROR: Cannot deduce output path from input path: %s\n", error->message);
            return EXIT_FAILURE;
        }

        auto const end = tr_strvJoin(base, ".torrent"sv);
        char* cwd = tr_getcwd();
        options.outfile = out2 = tr_buildPath(cwd, end.c_str(), nullptr);
        tr_free(cwd);
        tr_free(base);
    }

    if (std::empty(options.trackers))
    {
        if (options.is_private)
        {
            fprintf(stderr, "ERROR: no trackers specified for a private torrent\n");
            return EXIT_FAILURE;
        }
        else
        {
            printf("WARNING: no trackers specified\n");
        }
    }

    printf("Creating torrent \"%s\"\n", options.outfile);

    b = tr_metaInfoBuilderCreate(options.infile);

    if (b == nullptr)
    {
        fprintf(stderr, "ERROR: Cannot find specified input file or directory.\n");
        return EXIT_FAILURE;
    }

    if (options.piecesize_kib != 0)
    {
        tr_metaInfoBuilderSetPieceSize(b, options.piecesize_kib * KiB);
    }

    char buf[128];
    printf(
        b->fileCount > 1 ? " %" PRIu32 " files, %s\n" : " %" PRIu32 " file, %s\n",
        b->fileCount,
        tr_formatter_size_B(buf, b->totalSize, sizeof(buf)));
    printf(
        b->pieceCount > 1 ? " %" PRIu32 " pieces, %s each\n" : " %" PRIu32 " piece, %s\n",
        b->pieceCount,
        tr_formatter_size_B(buf, b->pieceSize, sizeof(buf)));

    tr_makeMetaInfo(
        b,
        options.outfile,
        std::data(options.trackers),
        std::size(options.trackers),
        options.comment,
        options.is_private,
        options.source);

    uint32_t last = UINT32_MAX;
    while (!b->isDone)
    {
        tr_wait_msec(500);

        uint32_t current = b->pieceIndex;
        if (current != last)
        {
            printf("\rPiece %" PRIu32 "/%" PRIu32 " ...", current, b->pieceCount);
            fflush(stdout);

            last = current;
        }
    }

    putc(' ', stdout);

    switch (b->result)
    {
    case TR_MAKEMETA_OK:
        printf("done!");
        break;

    case TR_MAKEMETA_URL:
        printf("bad announce URL: \"%s\"", b->errfile);
        break;

    case TR_MAKEMETA_IO_READ:
        printf("error reading \"%s\": %s", b->errfile, tr_strerror(b->my_errno));
        break;

    case TR_MAKEMETA_IO_WRITE:
        printf("error writing \"%s\": %s", b->errfile, tr_strerror(b->my_errno));
        break;

    case TR_MAKEMETA_CANCELLED:
        printf("cancelled");
        break;
    }

    putc('\n', stdout);

    tr_metaInfoBuilderFree(b);
    tr_free(out2);
    return EXIT_SUCCESS;
}
