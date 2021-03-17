#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <math.h>

// PROJECT 3

/* Name of the file supposed to be read - change it here. */
#define FILENAME "fat16_8.bin"

struct content{
    char **filenames;
    int *start_clusters_for_filenames;
    int lenght;
};

struct volume{
    uint8_t *arrFAT1;
    int x1;
    uint8_t *arrFAT2;
    int x2;
    uint8_t *arrROOT;
    int x3;
    uint32_t first_sector_of_data_block;
    int current_cluster_id;
    char *path;
};

struct __attribute__((packed)) temp{

    // 0-2 Assembly code inctruction to jump to boot code (mandatory in bootable partition)
    uint8_t jump[3];
    // 3-10 OEM name in ASCII (OEM ID)
    char oem_name[8];
    // 11-12 Bytes per sector (BPB)
    uint16_t bpb;
    // 13 Sectors per cluster
    char sectors_per_cluster;
    //
    uint16_t size_of_reserved_area;
    // size of reserved areain sectors
    char number_of_FAT; //
    // number of FATs
    uint16_t maximum_number_of_files;
    // maximum numver of files in the root directory (fat12/16, 0 for fat32)
    uint16_t sectors_in_file_system;
    // if 2B is not long enough, set to 0 and use 4B value in bytes 32-35 below
    char media_type;
    //
    uint16_t size_of_each_fat;
    // in sectors, for fat12/16, 0 for 32
    uint16_t sectors_per_track_storage;
    //
    uint16_t heads_storage;
    //
    uint32_t sectors_before_start_partition;
    //
    uint32_t sectors_in_file_system2;
    // the field will be 0 if the 2B field above is non zero
    char drive_number;
    // BIOS INT 13h (low level disk services) drive number
    char not_used;
    char boot_signature_extended;
    // Extended boot signature to validate next three fields (0x29)
    uint32_t volume_serial_number;
    // 39-42 	Volume serial number
    char volume_label[11];
    // 43-53 	Volume label, in ASCII
    char file_system_type_level[8];
    // 54-61 	File system type level, in ASCII. (Generally "FAT", "FAT12", or "FAT16")
    char not_used2[448];
    // 62-509 	Not used
    uint16_t signature_value;
    // 510-511 	Signature value (0xaa55)
};

struct  __attribute__((packed)) fat_name_t {
    char first_character;
    char name[7];
    char extension[3];
};

struct __attribute__((packed)) root_file{
    struct fat_name_t filename;
    uint8_t file_attributes;
    uint8_t reserved;
    uint8_t file_creation_time_s;
    uint16_t file_creation_time_h_m_s;
    uint16_t file_creation_date;
    uint16_t file_access_date;
    uint16_t high_order;
    uint16_t file_modified_time_h_m_s;
    uint16_t file_modified_date;
    uint16_t low_order;
    int file_size;
};

enum gcz{
    GET,
    CAT,
    ZIP
};

/*
  Read the given file.
*/

int  read_the_file (struct temp *temp, char *txt_name, FILE *f){

    f = fopen(txt_name, "rb");
    if( f == NULL )
        return 1;

    fseek(f, SEEK_SET, 0);
    fread(temp, sizeof(struct temp), 1, f);

    fclose(f);
    return 0;
}

/*
  Reads a block_count of data blocks, 512 bytes each.
*/

size_t readblock(void *buffer, uint32_t first_block, size_t block_count){

    if( buffer == NULL || first_block <= 0 || block_count <= 0 )
        return 0;

    FILE *f = f = fopen(FILENAME, "rb");
    if( f == NULL )
        return 0;
    fseek(f, first_block * 512, SEEK_SET);
    fread(buffer, 512, block_count, f);
    return block_count; // sectors read
}

/*
  Helper function: returns an int stored in i number of bites.
*/

int counting_bytes(int start, int end, uint16_t data){
    int x = 0;
    for( int i = start; i >= end; i-- ) {
        int bit = !!(data&(1ll<<i));
        if( bit != 0)
            x = x + pow(2, i - end);
    }
    return x;
}

/* */

int get_chain_length (struct volume *volume, struct temp *temp, int first_cluster) {

    if( volume == NULL || temp == NULL || first_cluster < 2 ) {
        return -1;
    }

    int next_index = first_cluster, counter = 1;

    while( next_index >= 0xFFF8 && next_index <= 0xFFFF ) {
        next_index = ((uint16_t*)volume->arrFAT1)[next_index];
        counter++;
    }

    return counter;
}

/* Reads n clusters from given position. */

uint32_t read_cluster(uint32_t first_sector_of_data_block, void* buffer, uint32_t cluster_id, struct temp *temp) {
    uint32_t first_sector = first_sector_of_data_block + (cluster_id - 2) * temp->sectors_per_cluster;
    uint32_t read = readblock(buffer, first_sector, temp->sectors_per_cluster);
    return read / temp->sectors_per_cluster;
}

uint32_t read_chain(struct volume *volume, struct temp *temp, void* buffer, uint32_t first_cluster, uint32_t start_position, uint32_t count) {
    if( volume->arrFAT1 == NULL || volume->arrROOT == NULL || temp == NULL || buffer == NULL || first_cluster < 2 || count <= 0 ) {
        return -1;
    }

    int n = 0, next_index = first_cluster, counter = 0, read_cluster_counter = 0, bytes_to_add = 0;
    uint32_t bpc = temp->sectors_per_cluster * temp->bpb;

    uint32_t vcluster = start_position / bpc;
    uint32_t vcluster_position = start_position - bpc * vcluster;

    uint32_t bytes_left_to_read = count;

    char *temp_buffer = realloc(NULL, sizeof( char ) * bpc );

    while( next_index >= 0x0002 && next_index <= 0xFFF6 ) {

        if( counter < vcluster ) {
            next_index = ((uint16_t*)volume->arrFAT1)[next_index];
        }

        else {
            /* Getting inside of the first place that is needed to read, continue until there is no bytes left. */
            n = read_cluster(volume->first_sector_of_data_block, temp_buffer, next_index, temp);

            if( read_cluster_counter == 0 ) {

                memcpy(buffer, temp_buffer + vcluster_position, (bpc - vcluster_position) > count ? count : (bpc - vcluster_position));

                bytes_left_to_read = bytes_left_to_read - (bpc - vcluster_position) > count ? count : (bpc - vcluster_position);
                bytes_to_add = (bpc - vcluster_position) > count ? count : (bpc - vcluster_position);

            }
            else {
                if( bytes_left_to_read >= bpc ) {
                    memmove(buffer + bytes_to_add, temp_buffer, bpc);
                    bytes_left_to_read = bytes_left_to_read - bpc;
                    bytes_to_add = bytes_to_add + bpc;
                } else if ( bytes_left_to_read != 0) {
                    memmove(buffer + bytes_to_add, temp_buffer, bytes_left_to_read);
                    bytes_to_add = bytes_to_add + bytes_left_to_read;
                    bytes_left_to_read = 0;
                    break;
                }
            }
            next_index = ((uint16_t*)volume->arrFAT1)[next_index];
            read_cluster_counter++;
        }
        counter++;
    }

    free(temp_buffer);
    return count;
}

void get_attribute(struct root_file *r) {
    printf( r->file_attributes & 0x01 ? "R+ " : "R- "); // tylko do odczytu
    printf( r->file_attributes & 0x02 ? "H+ " : "H- "); // plik ukryty
    printf( r->file_attributes & 0x04 ? "S+ " : "S- "); // plik systemowy
    printf( r->file_attributes & 0x08 ? "V+ " : "V- "); // etykieta woluminyu
    printf( r->file_attributes & 0x10 ? "D+ " : "D- "); // directory
    printf( r->file_attributes & 0x20 ? "A+ " : "A- "); // plik archiwalny
    printf( r->file_attributes & 0x0F ? "LFN " : " "); // dluga nazwa pliku
}

/* Helper functions printing dates. */

void get_date(uint16_t date) {
    int year = counting_bytes(15, 9, date) + 1980;
    int month = counting_bytes(8, 5, date);
    int day = counting_bytes(4, 0, date);
    printf("%02d/%02d/%02d", year, month, day);
}

void get_time(uint16_t time) {
    int hour = counting_bytes(15, 11, time);
    int minute = counting_bytes(10, 5, time);
    int second = counting_bytes(4, 0, time) / 2;
    printf("%02d:%02d:%02d", hour, minute, second);
}

int my_strcmp(char *s1, char *s2) {

    if( s1 == NULL || s2 == NULL )
        return -2;

    int l1 = strlen(s1);
    int l2 = strlen(s2);

    if( l1 != l2 )
        return -1;

    for( int i = 0; i < l1; i++ ) {
        if( (s1[i] >= 'a' && s1[i] <= 'z') && (s2[i] >= 'A' && s2[i] <='Z') ) {
            if( s1[i] - 32 != s2[i] )
                return -1;
        }
        else if( (s2[i] >= 'a' && s2[i] <= 'z') && (s1[i] >= 'A' && s1[i] <='Z') ) {
            if( s2[i] - 32 != s1[i] )
                return -1;
        }
        else if ( s1[i] != s2[i] )
            return -1;
    }

    return 0;
}

char* get_name_of_root_files(char first_letter, char* name, char* extension){

    char *n = realloc(NULL, sizeof(char) * ( 13 ) );
    n[0] = first_letter;
    n[1] = '\0';

    int counter = 1;
    for( int i = 0; i < 7; i++ ) {
        if( name[i] != ' ' ) {
            n[counter] = name[i];
            counter++;
        }
    }
    if( extension[0] != ' ' && extension[1] != ' ' && extension[2] != ' ' ) {
        n[counter] = '.';
        counter++;
        for( int i = 0; i < 3; i++ ) {
            if( extension[i] != ' ' ) {
                n[counter] = extension[i];
                counter++;
            }
        }
    }
    n[counter] = '\0';

    return n;
}

void print_for_dir (struct root_file *file) {
    if( !(file->file_attributes == 0x0F) ) {
        get_date(file->file_creation_date);
        printf("%3s", " ");
        get_time(file->file_creation_time_h_m_s);
        printf("%3s", " ");
        get_attribute(file);
        printf("[%s]\n", get_name_of_root_files(file->filename.first_character, file->filename.name, file->filename.extension));
    }
}

void dir(struct temp *temp, struct volume *volume, int id){

    if ( ((uint16_t*)volume->arrFAT1)[id] < 0xFFF8 ) {
        dir(temp, volume, ((uint16_t*)volume->arrFAT1)[id] );
    }

    if ( id == 0 ) {
        for( int i = 0; i < temp->maximum_number_of_files; i++ )
        {
            struct root_file *root_file;
            root_file = (struct root_file*)( (char*)volume->arrROOT + i * sizeof(struct root_file) );

            if( root_file->filename.first_character == '\0' || root_file->file_attributes == 0xf || root_file->filename.first_character == (char)0xE5)
                continue;

            print_for_dir(root_file);
        }
    } else {
        /* Not working on root, but ARRData! */
        int bpc = temp->sectors_per_cluster * temp->bpb;
        int max_number_of_files = bpc / sizeof(struct root_file);
        int chain_length = get_chain_length(volume, temp, volume->current_cluster_id);

        for( int i = 0; i < chain_length; i++ ) {
            /* Getting throught the whole length of the folder, as it can take more than one cluster! */

            char buffer[bpc];
            read_chain(volume, temp, buffer, volume->current_cluster_id, i * bpc, bpc);

            for( int j = 0; j < max_number_of_files; j++ ) {

                struct root_file *file = (struct root_file*)( buffer + j * sizeof(struct root_file) );

                if( file->filename.first_character == (char)0xE5 ) {
                    continue;
                } else if ( file->filename.first_character == '\0' ) {
                    return;
                }
                print_for_dir(file);
            }
        }
    }
    printf("\n\n");

    return;
}

void rootinfo( struct temp *temp, struct volume *volume) {

    int counter = 0;

    for( int i = 0; i < temp->maximum_number_of_files; i++ )
    {
        struct root_file *root_file;
        root_file = (struct root_file*)( (char*)volume->arrROOT + i * sizeof(struct root_file) );

        if( root_file->filename.first_character == '\0' || root_file->file_attributes == 0xf || root_file->filename.first_character == (char)0xE5 )
            continue;
        counter++;
    }

    printf("Liczba wpisow w katalogu glownym: %d\n", counter);
    printf("Maksymalna dopuszczalna liczba wpisow: %d\n", temp->maximum_number_of_files);
    float p = (float)counter / (float)temp->maximum_number_of_files;
    printf("Procentowe wypelnienie: %f%%\n\n", p);

}

void fileinfo(struct volume *volume, struct temp *temp, struct root_file *root_file) {

    if( root_file->filename.first_character == '\0' || root_file->file_attributes == 0xf || root_file->filename.first_character == (char)0xE5 )
        return;

    printf("Pełna nazwa: %s\n", get_name_of_root_files(root_file->filename.first_character, root_file->filename.name, root_file->filename.extension));
    printf("Atrybuty: ");
    get_attribute(root_file);
    printf("\nWielkość: %d\n", root_file->file_size);
    printf("Ostatni zapis: ");
    get_date(root_file->file_modified_date);
    get_time(root_file->file_modified_time_h_m_s);
    printf("\nOstatni dostęp: ");
    get_date(root_file->file_access_date);
    printf("\nUtworzono: ");
    get_date(root_file->file_creation_date);
    get_time(root_file->file_creation_time_h_m_s);
    printf("\nŁańcuch klastrów: ");
    int index = ((uint16_t*)volume->arrFAT1)[root_file->low_order];
    int cluster_counter = 0;

    if( (index >= 0x0002 && index <= 0xFFF6) || (index >= 0xFFF8 && index <= 0xFFFF) ) {
        printf("[%d]", root_file->low_order);

        while( index != 0 ) {
            if( index >= 0x0002 && index <= 0xFFF6 ) {
                printf("%" PRIu16, index);
                index  = ((uint16_t*)volume->arrFAT1)[index];
                cluster_counter++;
            } else if ( (index >= 0xFFF8 && index <= 0xFFFF) || index == 0x0000 || index == 0x0001 || index == 0xFFF7 ) {
                break;
            }
            printf(", ");
        }
        printf("\nLiczba klastrów: %d\n\n\n", cluster_counter);
    }
}

struct root_file* return_file (struct temp *temp, struct volume *volume, char *name) {

    if( temp == NULL || volume == NULL || name == NULL ) {
        printf("Podano bledne parametry. Funckaj return_file nie zostala wykonana.\n");
        return NULL;
    }

    if( volume->current_cluster_id == 0 ) { // jestem w roocie
        if ( ((uint16_t*)volume->arrFAT1)[0] < 0xFFF8 ) {
            dir(temp, volume, ((uint16_t*)volume->arrFAT1)[0] );
        }
        for( int i = 0; i < temp->maximum_number_of_files; i++ )
        {
            struct root_file *root_file;
            root_file = (struct root_file*)( (char*)volume->arrROOT + i * sizeof(struct root_file) );

            if( root_file->filename.first_character == '\0' || root_file->file_attributes == 0xf || root_file->filename.first_character == (char)0xE5)
                continue;

            char *filename = get_name_of_root_files(root_file->filename.first_character, root_file->filename.name, root_file->filename.extension);

            if( my_strcmp(name, filename) == 0 ) {

                struct root_file *found_file = (struct root_file*)realloc(NULL, sizeof(struct root_file));
                memcpy(found_file, root_file, sizeof(struct root_file));

                return found_file;
            }
        }
    }
    else {
        /* Working on ARRdata, not root! */
        int bpc = temp->sectors_per_cluster * temp->bpb;
        int max_number_of_files = bpc / sizeof(struct root_file);
        int chain_length = get_chain_length(volume, temp, volume->current_cluster_id);

        for( int i = 0; i < chain_length; i++ ) {
            /* Going throught the lentgh of the whole folder, as it can take more than one cluster! */

            char buffer[bpc];
            read_chain(volume, temp, buffer, volume->current_cluster_id, i * bpc, bpc);

            for( int j = 0; j < max_number_of_files; j++ ) {

                struct root_file *file = (struct root_file*)( buffer + j * sizeof(struct root_file) );

                if( file->filename.first_character == (char)0xE5 ) {
                    continue;
                }
                else if ( file->filename.first_character == '\0' ) {
                    return NULL;
                }

                char *filename = get_name_of_root_files(file->filename.first_character, file->filename.name, file->filename.extension);

                if( my_strcmp(name, filename) == 0 ) {

                    struct root_file *found_file = (struct root_file*)realloc(NULL, sizeof(struct root_file));
                    memcpy(found_file, file, sizeof(struct root_file));

                    return found_file;
                }
            }
        }

    }

    return NULL;
}

void cd(char *name, struct temp *temp, struct volume *volume) {

    if( name == NULL || temp == NULL || volume == NULL ) {
        printf("Podano bledne parametry. Funkcja cd nie zostala wykonana.\n");
        return;
    }

    if( volume->current_cluster_id == 0 && my_strcmp("..", name) == 0 ) {
        printf("Jestes w katalogu glownym!\n");
        printf("name: %s", name);
        return;
    }

    struct root_file *file = return_file(temp, volume, name);

    if( file == NULL ) {
        printf("Nie znaleziono pliku o nazwie: [%s]\n", name);
    } else {
        volume->current_cluster_id = file->low_order;
        if( strcmp(name, "..") == 0 ) { // user sie cofa
            int length = strlen(volume->path);
            for( int i = length - 1; i >= 0; i-- ) {
                if( volume->path[i] == '\\' ) {
                    volume->path[i] = '\0';
                }
            }
            printf("    Sciezka: %s\n\n", volume->path);
            volume->current_cluster_id = 0;
        } else {
            strcat(volume->path, "\\");
            strcat(volume->path, name);
            printf("\n    Wykonano funkcje cd.\n");
            printf("Aktualny numer katalogu: %d\n", volume->current_cluster_id);
            printf("    Sciezka: %s\n\n", volume->path);
        }
    }

}

void get_or_cat (struct temp *temp, struct volume *volume, char *name, FILE *f) {

    if( temp == NULL || volume == NULL || name == NULL ) {
        printf("Podano bledne parametry. Funkcja cat nie zostala wykonana.\n");
        return;
    }
    printf("ID aktualnego klastra: %d\n", volume->current_cluster_id);
    printf("Podana nazwa: [%s]\n", name);

    struct root_file *file = return_file(temp, volume, name);
    if( file == NULL ) {
        printf("    Plik o podanej nazwie nie istnieje.\n");
    } else {
        printf("   Znaleziono plik: [%s]\n\n", name);

        char *buffer = (char*)realloc(NULL, sizeof(char) * file->file_size);

        int bytes_left_to_read = file->file_size;
        int position = 0;

        while( bytes_left_to_read > 400 ) {
            read_chain(volume, temp, buffer, file->low_order, position, 400);
            bytes_left_to_read = bytes_left_to_read - 400;
            position = position + 400;
            for( int i = 0; i < 400; i++ ) {
                fprintf(f, "%c", buffer[i]);
            }
        }
        if( bytes_left_to_read < 400 && bytes_left_to_read != 0 ) {
            read_chain(volume, temp, buffer, file->low_order, position, bytes_left_to_read); // ??
            for( int i = 0; i < bytes_left_to_read; i++ ) {
                fprintf(f, "%c", buffer[i]);
            }
        }
        free(buffer);
    }
}

int print_line_of_file (struct temp *temp, struct volume *volume, char *name, struct root_file *file, FILE *f, int pos) {

    if( temp == NULL || volume == NULL || name == NULL ) {
        return -1;
    }

    char *buffer = (char*)realloc(NULL, sizeof(char) * file->file_size);

    int bytes_left_to_read = file->file_size - pos;
    int position = pos;
    int to_print = 0;
    int first_n = 0;

    while( bytes_left_to_read > 400 ) {
        read_chain(volume, temp, buffer, file->low_order, position, 400);
        for( int i = 0; i < 400; i++ ) {
            if( buffer[i] == '\n' ) {
                to_print++;
                first_n = 1;
                break;
            }
            to_print++;
        }

        bytes_left_to_read = bytes_left_to_read - to_print;
        position = position + to_print;
        for( int i = 0; i < to_print; i++ ) {
            fprintf(f, "%c", buffer[i]);
        }
        if( first_n == 1 ) {
            free(buffer);
            return position;
        }
    }
    if( bytes_left_to_read < 400 && bytes_left_to_read != 0 && first_n == 0 ) {
        read_chain(volume, temp, buffer, file->low_order, position, bytes_left_to_read); // ??

        for( int i = 0; i < bytes_left_to_read; i++ ) {
            if( buffer[i] == '\n' ) {
                to_print++;
                first_n = 1;
                break;
            }
            to_print++;
        }

        for( int i = 0; i < to_print; i++ ) {
            fprintf(f, "%c", buffer[i]);
        }
        free(buffer);
        return position + to_print;
    }
    free(buffer);

    return 0;
}

void cat (struct temp *temp, struct volume *volume, char *name) {
    get_or_cat(temp, volume, name, stdout);
}

void get (struct temp *temp, struct volume *volume, char *name) {
    FILE *f = fopen(name, "w");
    get_or_cat(temp, volume, name, f);
}

void zip (struct temp *temp, struct volume *volume, char *name1, char *name2, char *destination) {

    if( temp == NULL || volume == NULL || name1 == NULL || name2 == NULL || destination == NULL ) {
        printf("Podano bledne parametry. Funkcja zip nie zostala wykonana.\n");
        return;
    }

    struct root_file *file1 = return_file(temp, volume, name1);
    if( file1 == NULL ) {
        printf("Plik o nazwie [%s] nie istnieje. Funkcja nie zostala wykonana.\n", name1);
        return;
    }
    struct root_file *file2 = return_file(temp, volume, name2);
    if( file2 == NULL ) {
        printf("Plik o nazwie [%s] nie istnieje. Funkcja nie zostala wykonana.\n", name2);
        return;
    }

    int printed1 = 0, printed2 = 0;
    FILE *f = fopen(destination, "w");

    while( printed1 < file1->file_size || printed2 < file2->file_size ) {
        if( printed1 < file1->file_size ) {
            printed1 = print_line_of_file(temp, volume, name1, file1, f, printed1);
        }
        if (printed2 < file2->file_size) {
            printed2 = print_line_of_file(temp, volume, name2, file2, f, printed2);
        }
    }

}

void pwd (struct temp *temp, struct volume *volume) {

    printf("ID aktualnego klastra: %d\n", volume->current_cluster_id);

    if( volume->current_cluster_id != 0 ) {
        struct root_file *current_directory;
        current_directory = return_file(temp, volume, ".");

        printf("    Nazwa aktualnego klastra: %s\n\n", get_name_of_root_files(current_directory->filename.first_character, current_directory->filename.name, current_directory->filename.extension));
        printf("    Sciezka: %s", volume->path);
    } else {
        printf("    Sciezka: \\");
    }
}

void countring_clusters(int index, int *cfree, int *ctaken, int *cbroken, int *cending) {
    if( index == 0x0000) {
        (*cfree)++;
    } else if ( index == 0x0001 ) {
    } else if ( index >= 0x0002 && index <= 0xFFF6 ) {
        (*ctaken)++;
    } else if ( index == 0xFFF7 ) {
        (*cbroken)++;
    } else if ( index >= 0xFFF8 && index <= 0xFFFF ) {
        (*cending)++;
    }
}

void count_all_clusters(struct volume *volume, struct temp *temp, int id, int *cfree, int *ctaken, int *cbroken, int *cending) {

    int bpc = temp->sectors_per_cluster * temp->bpb;
    int max_number_of_files = bpc / sizeof(struct root_file);
    int chain_length = get_chain_length(volume, temp, id);

    for( int i = 0; i < chain_length; i++ ) {

        char buffer[bpc];
        read_chain(volume, temp, buffer, id, i * bpc, bpc);

        for( int j = 0; j < max_number_of_files; j++ ) {

            struct root_file *file = (struct root_file*)( buffer + j * sizeof(struct root_file) );

            if( file->filename.first_character == (char)0xE5 ) {
                continue;
            }
            else if ( file->filename.first_character == '\0' ) {
                return;
            }

            int index = file->low_order;

            if ( file->file_attributes == 0x20 ) {
                count_all_clusters(volume, temp, id, cfree, ctaken, cbroken, cending);
            } else {
                countring_clusters(index, cfree, ctaken, cbroken, cending);
                if ( index >= 0xFFF8 && index <= 0xFFFF ) {
                    break;
                }
                index  = ((uint16_t*)volume->arrFAT1)[index];
            }
        }
    }
}

void spaceinfo(struct temp *temp, struct volume *volume) {

    int ctaken = 0, cfree = 0, cbroken = 0, cending = 0;

    for( int i = 0; i < temp->maximum_number_of_files; i++ )
    {
        struct root_file *root_file;
        root_file = (struct root_file*)( (char*)volume->arrROOT + i * sizeof(struct root_file) );

        if( root_file->filename.first_character == '\0' || root_file->file_attributes == 0xf || root_file->filename.first_character == (char)0xE5)
            continue;

        int index = root_file->low_order;

        while( 1 ) {
            countring_clusters(index, &cfree, &ctaken, &cbroken, &cending);
            if ( index >= 0xFFF8 && index <= 0xFFFF ) {
                break;
            }
            index  = ((uint16_t*)volume->arrFAT1)[index];
        }
    }

    printf("Liczba klastrow zajetych: \n");
    printf("   %d\n", ctaken);
    printf("Liczba klastrow wolnych: \n");
    printf("   %d\n", cfree);
    printf("Liczba klastrow uszkodzonych: \n");
    printf("   %d\n", cbroken);
    printf("Liczba klastrow konczacych lancuchy klastrow: \n");
    printf("   %d\n", cending);
    printf("Wielkosc klastra w bajtach i sektorach:\n");
    printf("   SEKTORY: %d, BAJTY: %d\n", temp->sectors_per_cluster, temp->bpb);
}

int main(int argc, char** argv) {

    if( argc == 0 )
        return 1;

    FILE *f = NULL;
    char txt_name[30];
    strcpy(txt_name, FILENAME);

    struct temp temp;
    if( read_the_file(&temp, txt_name, f) == 1 )
    {
        printf("Couldn't open the file\n");
        return 4;
    }

    int size = temp.bpb * temp.size_of_each_fat;
    struct volume volume;
    char path[255] = {0};
    volume.path = path;

    volume.arrFAT1 = realloc( NULL, sizeof(uint8_t) * size );
    volume.arrFAT2 = realloc( NULL, sizeof(uint8_t) * size );
    volume.x1 = readblock(volume.arrFAT1, temp.size_of_reserved_area, temp.size_of_each_fat);
    volume.x2 = readblock(volume.arrFAT2, temp.size_of_reserved_area + volume.x1, temp.size_of_each_fat);

    printf("number of fats: %d", temp.number_of_FAT); // ??? tu jest problem, lmao?
    printf("\n[%d %d]\n", volume.x1, volume.x2);
    int a = memcmp(volume.arrFAT1, volume.arrFAT2, sizeof(uint8_t) * size);
    printf("a: %d\n\n", a);

    volume.arrROOT = realloc( NULL, sizeof(struct root_file) * temp.maximum_number_of_files );
    volume.x3 = readblock(volume.arrROOT, temp.size_of_reserved_area + volume.x1 + volume.x2, ( temp.maximum_number_of_files * sizeof(struct root_file) ) / 512 );

    volume.first_sector_of_data_block = temp.size_of_reserved_area + volume.x1 + volume.x2 + volume.x3; // 1 sektor bloku danych czyli klastra numer 2

    struct root_file **root_files = realloc(NULL, sizeof(struct root_file*) * temp.maximum_number_of_files );

    rootinfo(&temp, &volume);

    for( int i = 0; i < temp.maximum_number_of_files; i++ )
    {
        struct root_file *root_file;
        root_file = (struct root_file*)( (char*)volume.arrROOT + i * sizeof(struct root_file) );
        root_files[i] = root_file;

        if( root_file->filename.first_character == '\0' || root_file->file_attributes == 0xf || root_file->filename.first_character == (char)0xE5)
            continue;

        fileinfo(&volume, &temp, root_file);
    }

    volume.current_cluster_id = 0;
    spaceinfo(&temp, &volume);
    dir(&temp, &volume, volume.current_cluster_id);
    cd("N1", &temp, &volume);
    cd("..", &temp, &volume);
    cat(&temp, &volume, "lorem");
    //cd("folder1", &temp, &volume);
    cat(&temp, &volume, "AAA.TXT");


    //cat(&temp, &volume, "ala");
    //cat(&temp, &volume, "8_znakow");
    //get(&temp, &volume, "8_znakow");
    //zip(&temp, &volume, "ala", "8_znakow", "zip");
    dir(&temp, &volume, volume.current_cluster_id);

    return 0;
}
