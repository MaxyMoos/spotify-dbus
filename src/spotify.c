#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <dbus/dbus.h>

#define DEBUG 0
#define MAXSIZE 100


typedef struct {
    char *key;
    int dbus_type;
    void *value;
} MetadataItem;

typedef struct {
    MetadataItem meta[MAXSIZE];
    uint32_t curIndex;
} MetadataArray;

typedef enum {
    VALUE_NOT_FOUND,
    VALUE_FOUND,
    WRONG_TYPE
} GetMetadataResult;

void init_metadata_array(MetadataArray *arr)
{
    arr->curIndex = 0;
}

void free_metadata_array(MetadataArray *arr)
{
    for (uint32_t i = 0; i < arr->curIndex; ++i) {
        free(arr->meta[i].key);
        free(arr->meta[i].value);
    }
}

void insert_metadata(MetadataArray *arr, const char *key, int dbus_type, const void *value, size_t size)
{
    if (arr->curIndex >= MAXSIZE) {
        fprintf(stderr, "ERROR: metadata array is full\n");
        return;
    }

    MetadataItem *m = &arr->meta[arr->curIndex];
    m->key = strdup(key);
    m->dbus_type = dbus_type;
    if (dbus_type == DBUS_TYPE_STRING) {
        m->value = strdup((char*)value);
    } else {
        m->value = malloc(size);
        if (m->value != NULL) {
            memcpy(m->value, value, size);
        } else {
            fprintf(stderr, "ERROR: could not allocate memory\n");
        }
    }
    arr->curIndex++;
}

GetMetadataResult get_value(MetadataArray *arr, const char *key, int dbus_type, void *outValue)
{
    for (uint32_t i = 0; i < arr->curIndex; ++i) {
        if (strcmp(arr->meta[i].key, key) == 0) {
            if (arr->meta[i].dbus_type != dbus_type) {
                return WRONG_TYPE;
            }
            switch (dbus_type) {
                case DBUS_TYPE_INT32:
                    *((int32_t*)outValue) = *((int32_t*)arr->meta[i].value);
                    break;
                case DBUS_TYPE_STRING:
                    *((char**)outValue) = strdup((char*)arr->meta[i].value);
                    break;
                case DBUS_TYPE_UINT64:
                    *((uint64_t*)outValue) = *((uint64_t*)arr->meta[i].value);
                    break;
                default:
                    return VALUE_NOT_FOUND;
            }
            return VALUE_FOUND;
        }
    }
    return VALUE_NOT_FOUND;
}

void print_metadata_array(MetadataArray arr)
{
    MetadataItem *tmp;
    for (uint32_t i = 0; i < arr.curIndex; ++i) {
        tmp = &arr.meta[i];
        printf("Metadata item %d:\n\tdbus_type = %d\n\tkey = %s\n\tvalue = ", i, tmp->dbus_type, tmp->key);
        switch (tmp->dbus_type) {
            case DBUS_TYPE_STRING:
                printf("%s\n", (char*)tmp->value);
                break;
            case DBUS_TYPE_INT32:
                printf("%d\n", *((int32_t*)tmp->value));
                break;
            case DBUS_TYPE_UINT64:
                printf("%" PRIu64 "\n", *((uint64_t*)tmp->value));
                break;
            default:
                printf("Unsupported type\n");
                break;
        }
    }
}

void check_error(DBusError *error)
{
    if (dbus_error_is_set(error)) {
        if (strcmp(error->name, "org.freedesktop.DBus.Error.ServiceUnknown") == 0) {
            printf("ERROR: is Spotify running?\n");
        } else {
            printf("ERROR: %s\n", error->message);
        }
        dbus_error_free(error);
        exit(1);
    }
}

void process_variant(DBusMessageIter *variant, const char *key, MetadataArray *meta)
{
    int varType = dbus_message_iter_get_arg_type(variant);

    int32_t ui32Val;
    uint64_t ui64Val;
    double dblVal;
    char *strVal;
    void *output = NULL;
    size_t outputSize;
    DBusMessageIter arr;

    switch (varType) {
        case DBUS_TYPE_STRING:
            dbus_message_iter_get_basic(variant, &strVal);
            if (DEBUG) printf("\tString: %s\n", strVal);
            output = strVal;
            outputSize = sizeof(char) * strlen(strVal);
            break;
        case DBUS_TYPE_INT32:
            dbus_message_iter_get_basic(variant, &ui32Val);
            if (DEBUG) printf("\tInt32: %d\n", ui32Val);
            output = (void*)&ui32Val;
            outputSize = sizeof(int32_t);
            break;
        case DBUS_TYPE_UINT64:
            dbus_message_iter_get_basic(variant, &ui64Val);
            if (DEBUG) printf("\tUInt64: %zu\n", ui64Val);
            output = (void*)&ui64Val;
            outputSize = sizeof(uint64_t);
            break;
        case DBUS_TYPE_DOUBLE:
            dbus_message_iter_get_basic(variant, &dblVal);
            if (DEBUG) printf("\tDouble: %f\n", dblVal);
            output = (void*)&dblVal;
            outputSize = sizeof(double);
            break;
        case DBUS_TYPE_ARRAY:
            dbus_message_iter_recurse(variant, &arr);
            while ((dbus_message_iter_get_arg_type(&arr)) != DBUS_TYPE_INVALID) {
                process_variant(&arr, key, meta);
                dbus_message_iter_next(&arr);
            }
            break;
        default:
            printf("\tUnhandled variant type: %d\n", varType);
    }
    if (output != NULL) {
        insert_metadata(meta, key, varType, output, outputSize);
    }
}

void print_usage()
{
    printf("usage: spotify-dbus [command]\n\n  COMMANDS:\n");
    printf("    track: display current track artist+title\n");
}

int main(int argc, char *argv[]) 
{
    int retval = 0;

    MetadataArray metadata;
    DBusError error;
    DBusConnection *conn;
    DBusMessage *msg, *reply;
    DBusMessageIter args, iter_array, dict_entry, dict, variant;
    char *key;

    dbus_error_init(&error);
    init_metadata_array(&metadata);

    conn = dbus_bus_get(DBUS_BUS_SESSION, &error);
    check_error(&error);

    msg = dbus_message_new_method_call(
        "org.mpris.MediaPlayer2.spotify",   // target for the method call
        "/org/mpris/MediaPlayer2",          // object to call on
        "org.freedesktop.DBus.Properties",  // interface to call on
        "Get"                               // method name
    );
    if (msg == NULL) {
        printf("Message was NULL\n");
        exit(1);
    }

    const char *interface_name = "org.mpris.MediaPlayer2.Player";
    const char *property_name = "Metadata";
    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &interface_name);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &property_name);

    // Send the message & get a handle for the reply
    reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &error);
    check_error(&error);

    // Read metadata iteratively
    if (dbus_message_iter_init(reply, &args)) {
        dbus_message_iter_recurse(&args, &iter_array);

        while (dbus_message_iter_get_arg_type(&iter_array) != DBUS_TYPE_INVALID) {
            dbus_message_iter_recurse(&iter_array, &dict_entry);
            
            while (dbus_message_iter_get_arg_type(&dict_entry) != DBUS_TYPE_INVALID) {
                dbus_message_iter_recurse(&dict_entry, &dict);
                dbus_message_iter_get_basic(&dict, &key);
                if (DEBUG) printf("%s\n", key);
    
                dbus_message_iter_next(&dict);
                dbus_message_iter_recurse(&dict, &variant);

                process_variant(&variant, key, &metadata);
                dbus_message_iter_next(&dict_entry);
            }

            dbus_message_iter_next(&iter_array);
        }
    } else {
        printf("Reply does not have arguments!\n");
    }

    // Free the message & connection
    dbus_message_unref(msg);
    dbus_message_unref(reply);
    dbus_connection_unref(conn);

    if (argc > 1) {
        if (strcmp(argv[1], "track") == 0) {
            char *artist = NULL;
            char *title = NULL;
            GetMetadataResult ret1 = get_value(&metadata, "xesam:artist", DBUS_TYPE_STRING, &artist);
            GetMetadataResult ret2 = get_value(&metadata, "xesam:title", DBUS_TYPE_STRING, &title);

            if (ret1 != VALUE_FOUND || ret2 != VALUE_FOUND) {
                fprintf(stderr, "Could not read artist/track metadata.\n");
                retval = 1;
            } else {
                printf("%s - %s", artist, title);
                retval = 0;
            }
            free(artist);
            free(title);
        }
    } else {
        print_usage();
    }

    free_metadata_array(&metadata);

    return retval;
}
