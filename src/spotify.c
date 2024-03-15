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

/**
 * Initialize a MetadataArray
 */
void init_metadata_array(MetadataArray *arr)
{
    arr->curIndex = 0;
}

/**
 * Free all the dynamically-allocated members in a MetadataArray
 */
void free_metadata_array(MetadataArray *arr)
{
    for (uint32_t i = 0; i < arr->curIndex; ++i) {
        free(arr->meta[i].key);
        free(arr->meta[i].value);
    }
}

/**
 * Append a new metadata item to a MetadataArray
 *
 * @param arr           Pointer to the MetadataArray the new item will be appended to
 * @param key           The metadata item key
 * @param dbus_type     Integer representing the metadata value type
 * @param value         Pointer to the metadata value (its actual type depending on dbus_type)
 * @param size          The value size in bytes
 */
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
            fprintf(stderr, "ERROR: could not allocate memory for metadata item value\n");
        }
    }
    arr->curIndex++;
}

/**
 * Retrieves a metadata value from a MetadataArray based on a given key and expected dbus_type.
 *
 * This function searches the provided MetadataArray for an item that matches the specified key
 * and dbus_type. If a matching item is found, its value is copied to the location pointed to by
 * outValue. The function ensures type safety by matching the dbus_type of the requested key
 * with the type provided by the caller. If the types do not match, or if the key is not found,
 * appropriate status codes are returned. For string values, the function allocates memory for
 * a duplicate of the string, which the caller is responsible for freeing.
 *
 * Note: The caller must ensure that outValue points to a memory location that is suitable for
 * the type of data being requested. For instance, if dbus_type is DBUS_TYPE_INT32, outValue
 * should point to an int32_t variable.
 *
 * @param arr       Pointer to the MetadataArray from which the value is to be retrieved.
 * @param key       The key corresponding to the metadata item to be retrieved.
 * @param dbus_type The D-Bus type of the metadata item. This is used to ensure the type of the
 *                  stored value matches the expected type of the outValue pointer.
 * @param outValue  Pointer to the memory location where the retrieved value will be stored. The
 *                  type of data stored depends on the dbus_type parameter.
 *
 * @return GetMetadataResult enum value indicating the outcome of the operation:
 *         VALUE_NOT_FOUND if the key is not found in the array,
 *         WRONG_TYPE if the found item does not match the expected dbus_type,
 *         VALUE_FOUND if the item is found and successfully copied to outValue.
 */
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

/**
 * Prints all key/value pairs in a MetadataArray to stdout
 */
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
            fprintf(stderr, "ERROR: is Spotify running?\n");
        } else {
            fprintf(stderr, "ERROR: %s\n", error->message);
        }
        dbus_error_free(error);
        exit(1);
    }
}

/**
 * Processes a DBusMessageIter and adds the key/values encountered into a MetadataArray
 */
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
    printf("    track       print current track artist+title\n");
    printf("    metadata    print out all available metadata\n");
}

// N.B.: `metadata` is expected to have already been initialized with init_metadata_array
void get_dbus_metadata(DBusConnection *conn, MetadataArray *metadata, DBusError *error)
{
    DBusMessage *msg, *reply;
    DBusMessageIter args, iter_array, dict_entry, dict, variant;
    char *key;

    msg = dbus_message_new_method_call(
        "org.mpris.MediaPlayer2.spotify",   // target for the method call
        "/org/mpris/MediaPlayer2",          // object to call on
        "org.freedesktop.DBus.Properties",  // interface to call on
        "Get"                               // method name
    );
    if (msg == NULL) {
        fprintf(stderr, "ERROR: DBus message was NULL\n");
        exit(1);
    }

    const char *interface_name = "org.mpris.MediaPlayer2.Player";
    const char *property_name = "Metadata";

    dbus_message_iter_init_append(msg, &args);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &interface_name);
    dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &property_name);

    // Send the message & get a handle for the reply
    reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, error);
    check_error(error);

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

                process_variant(&variant, key, metadata);
                dbus_message_iter_next(&dict_entry);
            }

            dbus_message_iter_next(&iter_array);
        }
    } else {
        printf("Reply does not have arguments!\n");
    }

    // Free the message
    dbus_message_unref(msg);
    dbus_message_unref(reply);
}

/**
 * `track` command: prints out "[ARTIST] - [TITLE]" (typically for i3 status bar usage)
 */
int command_track(DBusConnection *conn, DBusError *error) // MetadataArray *metadata)
{
    int retval = 0;
    char *artist = NULL;
    char *title = NULL;
    MetadataArray metadata;

    init_metadata_array(&metadata);
    get_dbus_metadata(conn, &metadata, error);
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
    free_metadata_array(&metadata);

    return retval;
}

int command_metadata(DBusConnection *conn, DBusError *error)
{
    int retval = 0;
    MetadataArray metadata;

    init_metadata_array(&metadata);
    get_dbus_metadata(conn, &metadata, error);
    print_metadata_array(metadata);
    free_metadata_array(&metadata);
    return retval;
}

int main(int argc, char *argv[])
{
    int retval = 0;
    DBusError error;
    DBusConnection *conn;

    dbus_error_init(&error);
    conn = dbus_bus_get(DBUS_BUS_SESSION, &error);
    check_error(&error);

    if (argc > 1) {
        if (strcmp(argv[1], "track") == 0) {
            retval = command_track(conn, &error);
        } else if (strcmp(argv[1], "metadata") == 0) {
            retval = command_metadata(conn, &error);
        } else {
            printf("Command not supported.\n");
            print_usage();
        }
    } else {
        print_usage();
    }

    // Free the DBus connection
    dbus_connection_unref(conn);

    return retval;
}
