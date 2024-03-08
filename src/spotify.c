#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dbus/dbus.h>


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

void process_variant(DBusMessageIter *variant)
{
    int varType = dbus_message_iter_get_arg_type(variant);

    int32_t ui32Val;
    uint64_t ui64Val;
    double dblVal;
    char *strVal;
    DBusMessageIter arr;

    switch (varType) {
        case DBUS_TYPE_STRING:
            dbus_message_iter_get_basic(variant, &strVal);
            printf("\tString: %s\n", strVal);
            break;
        case DBUS_TYPE_INT32:
            dbus_message_iter_get_basic(variant, &ui32Val);
            printf("\tInt32: %d\n", ui32Val);
            break;
        case DBUS_TYPE_UINT64:
            dbus_message_iter_get_basic(variant, &ui64Val);
            printf("\tUInt64: %zu\n", ui64Val);
            break;
        case DBUS_TYPE_DOUBLE:
            dbus_message_iter_get_basic(variant, &dblVal);
            printf("\tDouble: %f\n", dblVal);
            break;
        case DBUS_TYPE_ARRAY:
            dbus_message_iter_recurse(variant, &arr);
            while ((dbus_message_iter_get_arg_type(&arr)) != DBUS_TYPE_INVALID) {
                process_variant(&arr);
                dbus_message_iter_next(&arr);
            }
            break;
        default:
            printf("\tUnhandled variant type: %d\n", varType);
    }
}

int main(void) 
{
    DBusError error;
    DBusConnection *conn;
    DBusMessage *msg, *reply;
    DBusMessageIter args, iter_array, dict_entry, dict, variant;
    char *key;

    dbus_error_init(&error);

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
                printf("%s\n", key);
    
                dbus_message_iter_next(&dict);
                dbus_message_iter_recurse(&dict, &variant);

                process_variant(&variant);
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

    return 0;
}
