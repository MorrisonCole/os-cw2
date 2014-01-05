#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"


void print_indent(int indent)
{
    int i;
    for (i = 0; i < indent; i++)
        printf(" ");
}

void copy_out_file(int usedClusters[], uint16_t cluster, uint32_t bytes_remaining, uint8_t *image_buf, struct bpb33* bpb)
{
    usedClusters[cluster] = 1;
    // printf("MORRISON - File cluster: %i\n", cluster);
    int total_clusters, clust_size;

    clust_size = bpb->bpbSecPerClust * bpb->bpbBytesPerSec;
    total_clusters = bpb->bpbSectors / bpb->bpbSecPerClust;

    if (cluster == 0) {
        fprintf(stderr, "Bad file termination\n");
        return;
    } else if (is_end_of_file(cluster)) {
        return;
    } else if (cluster > total_clusters) {
        abort(); /* this shouldn't be able to happen */
    }

    if (bytes_remaining <= clust_size) {
        /* this is the last cluster */
    } else {
        /* more clusters after this one */

        /* recurse, continuing to copy */
        copy_out_file(usedClusters, get_fat_entry(cluster, image_buf, bpb), bytes_remaining - clust_size, image_buf, bpb);
    }
    return;
}

/* TODO: Should take a collection 'unusedClusters' of uint16_t. Every time a cluster is part
 * of a file or is marked as free in the FAT, we remove it from the collection.
 * The elements left in the collection at the end of this method will then be parts of lost files. */
void check_lost_files(int usedClusters[], uint16_t cluster, int indent, uint8_t *image_buf, struct bpb33* bpb)
{
    // A value of 1 means that the cluster is used somewhere.
    usedClusters[cluster] = 1;

    struct direntry *dirent;
    int d, i;
    dirent = (struct direntry*) cluster_to_addr(cluster, image_buf, bpb);

    while (1) {
        for (d = 0; d < bpb->bpbBytesPerSec * bpb->bpbSecPerClust; d += sizeof(struct direntry)) {
            char name[9];
            char extension[4];
            uint32_t size;
            uint16_t file_cluster;
            name[8] = ' ';
            extension[3] = ' ';
            memcpy(name, &(dirent->deName[0]), 8);
            memcpy(extension, dirent->deExtension, 3);

            if (name[0] == SLOT_EMPTY)
                return;

            /* skip over deleted entries */
            if (((uint8_t)name[0]) == SLOT_DELETED)
                continue;

            /* names are space padded - remove the spaces */
            for (i = 8; i > 0; i--) {
                if (name[i] == ' ')
                    name[i] = '\0';
                else
                    break;
            }

            /* remove the spaces from extensions */
            for (i = 3; i > 0; i--) {
                if (extension[i] == ' ')
                    extension[i] = '\0';
                else
                    break;
            }

            /* don't print "." or ".." directories */
            if (strcmp(name, ".") == 0) {
                dirent++;
                continue;
            }
            if (strcmp(name, "..") == 0) {
                dirent++;
                continue;
            }

            if ((dirent->deAttributes & ATTR_VOLUME) != 0) {
                printf("Volume: %s\n", name);
            } else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
                print_indent(indent);
                printf("%s (directory)\n", name);
                file_cluster = getushort(dirent->deStartCluster);
                check_lost_files(usedClusters, file_cluster, indent+2, image_buf, bpb);
            } else {
                /* We have a file. We should follow the file and remove all the used clusters from our collection! */
                size = getulong(dirent->deFileSize);
                uint16_t file_cluster_begin = getushort(dirent->deStartCluster);
                printf("MORRISON - File cluster BEGIN: %i\n", file_cluster_begin);
                //uint16_t cluster, uint32_t bytes_remaining, uint8_t *image_buf, struct bpb33* bpb
                copy_out_file(usedClusters, file_cluster_begin, size, image_buf, bpb);

                print_indent(indent);
                printf("%s.%s (%u bytes)\n", name, extension, size);
            }

            dirent++;
        }

        /* We've reached the end of the cluster for this directory. Where's the next cluster? */
        if (cluster == 0) {
            // root dir is special
            dirent++;
        } else {
            cluster = get_fat_entry(cluster, image_buf, bpb);
            dirent = (struct direntry*) cluster_to_addr(cluster, image_buf, bpb);
        }
    }
}

void usage()
{
    fprintf(stderr, "Usage: dos_scandisk <imagename>\n");
    exit(1);
}

int main(int argc, char** argv)
{
    uint8_t *image_buf;
    int fd;
    struct bpb33* bpb;
    if (argc != 2) {
        usage();
    }

    image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);
    // TODO: Need to pass in an array with no. of elements = no. of clusters.
    int total_clusters = bpb->bpbSectors / bpb->bpbSecPerClust;
    int used_clusters[total_clusters];
    check_lost_files(used_clusters, 0, 0, image_buf, bpb);

    printf("Unreferenced: ");
    int i;
    for (i = 2; i < total_clusters; i++) {
        if (used_clusters[i] == 0 && get_fat_entry(i, image_buf, bpb) != CLUST_FREE) {
            printf("%i ", i);
        }

        if (i == total_clusters - 1) {
            printf("\n");
        }
    }
    close(fd);
    exit(0);
}
