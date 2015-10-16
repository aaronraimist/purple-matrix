/**
 * Interface to the matrix client/server API
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111-1301  USA
 */

#include "matrix-api.h"

/* std lib */
#include <string.h>

/* json-glib */
#include <json-glib/json-glib.h>

/* libpurple */
#include <debug.h>

#include "libmatrix.h"

typedef struct {
    MatrixAccount *account;
    MatrixApiCallback callback;
    gpointer user_data;
} MatrixApiRequestData;


/**
 * Parse the body of an API response
 *
 * @param ret_data       pointer to the response data
 * @param parser         returns a pointer to the JsonParser, or NULL if there
 *                       is no body. Use g_object_unref() to release it.
 * @param error_message  returns a pointer to an error message on error. Do not
 *                       free this.
 * @return a pointer to the start of the body, or NULL if there is no body
 */
static const gchar *matrix_api_parse_body(const gchar *ret_data,
                                          JsonParser **parser,
                                          const gchar **error_message)
{
    const gchar *body_pointer;
    GError *err = NULL;

    g_assert(parser != NULL);
    g_assert(error_message != NULL);
    
    *parser = NULL;
    *error_message = NULL;

    /* find the start of the body */
    body_pointer = strstr(ret_data, "\r\n\r\n");

    if(body_pointer == NULL) {
        /* no separator */
        return NULL;
    }

    body_pointer += 4;
    if(*body_pointer == '\0') {
        /* empty body */
        return NULL;
    }
    
    /* we have a body - parse it as JSON */
    *parser = json_parser_new();
    if(!json_parser_load_from_data(*parser, body_pointer, -1, &err)) {
        purple_debug_info("matrixprpl",
                          "unable to parse JSON: %s\n",
                          err->message);
        *error_message = _("Error parsing response");
        g_error_free(err);
        return body_pointer;
    }

    return body_pointer;
}

/**
 * The callback we give to purple_util_fetch_url_request - does some
 * initial processing of the response
 */
static void matrix_api_complete(PurpleUtilFetchUrlData *url_data,
                                gpointer user_data,
                                const gchar *ret_data,
                                gsize ret_len,
                                const gchar *error_message)
{
    MatrixApiRequestData *data = (MatrixApiRequestData *)user_data;
    int response_code = -1;
    gchar *response_message = NULL;
    JsonParser *parser;
    JsonNode *root;
    const gchar *body_start;
    
    if (!error_message) {
        /* parse the response line */
        gchar *response_line;
        gchar **splits;
        gchar *ptr;
        
        ptr = strchr(ret_data, '\r');
        response_line = g_strndup(ret_data,
                                  ptr == NULL ? ret_len : ptr - ret_data);
        splits = g_strsplit(response_line, " ", 3);

        if(splits[0] == NULL || splits[1] == NULL || splits[2] == NULL) {
            /* invalid response line */
            purple_debug_info("matrixprpl",
                              "unable to parse response line %s\n",
                              response_line);
            error_message = _("Error parsing response");
        } else {
            response_code = strtol(splits[1], NULL, 10);
            response_message = g_strdup(splits[2]);
        }
        g_free(response_line);
        g_strfreev(splits);
    }

    if (!error_message) {
        body_start = matrix_api_parse_body(ret_data, &parser, &error_message);
        if(parser)
            root = json_parser_get_root(parser);
    }

    (data->callback)(data->account, data->user_data,
                     response_code, response_message,
                     body_start, root, error_message);

    /* free the JSON parser, and all of the node structures */
    if(parser)
        g_object_unref(parser);
    
    g_free(data);
    g_free(response_message);
}

/**
 * Start an HTTP call to the API
 *
 * @param max_len maximum number of bytes to return from the request. -1 for
 *                default (512K).
 */
static PurpleUtilFetchUrlData *matrix_api_start(const gchar *url,
                                                MatrixAccount *account,
                                                MatrixApiCallback callback,
                                                gpointer user_data,
                                                gssize max_len)
{
    MatrixApiRequestData *data;

    data = g_new0(MatrixApiRequestData, 1);
    data->account = account;
    data->callback = callback;
    data->user_data = user_data;

    /* TODO: implement the per-account proxy settings */

    purple_debug_info("matrixprpl", "sending HTTP request to %s", url);
    
    return purple_util_fetch_url_request_len(url, TRUE, NULL, TRUE, NULL,
                                             TRUE, max_len,
                                             matrix_api_complete, data);
}


PurpleUtilFetchUrlData *matrix_initialsync(MatrixAccount *account,
                                           MatrixApiCallback callback,
                                           gpointer user_data)
{
    gchar *url;
    PurpleUtilFetchUrlData *fetch_data;
    
    url = g_strdup_printf("https://%s/_matrix/client/api/v1/initialSync?"
                          "access_token=%s",
                          account->homeserver, account->access_token);

    /* XXX: stream the response, so that we don't need to allocate so much
     * memory? But it's JSON
     */
    fetch_data = matrix_api_start(url, account, callback, user_data,
                                  10*1024*1024);
    g_free(url);
    
    return fetch_data;
}
