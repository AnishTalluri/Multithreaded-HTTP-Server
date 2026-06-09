// Asgn 2: A simple HTTP server.
// By: Eugene Chou
//     Andrew Quinn
//     Brian Zhao

#include "asgn2_helper_funcs.h"
#include "connection.h"
#include "debug.h"
#include "hashtable.h"
// #include "protocol.h"
#include "queue.h"
#include "response.h"
#include "request.h"
#include "rwlock.h"

#include <getopt.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

queue_t *request_queue;               // Queue for connection descriptors
pthread_t *worker_threads;            // Array of worker threads
pthread_mutex_t queue_mutex;          // Mutex for queue access
pthread_cond_t queue_cond;            // Condition variable for queue
HashTable *file_locks;                // Global Hashtable for File Locks
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex for logging


void handle_connection(int);
void handle_get(conn_t *);
void handle_put(conn_t *);
void handle_unsupported(conn_t *);

void *worker_thread(void *arg) {
    (void) arg;

    while (1) {
        int connfd;

        // Lock and wait for task
        pthread_mutex_lock(&queue_mutex);

        while(!queue_pop(request_queue, (void **) &connfd)) {
            pthread_cond_wait(&queue_cond, &queue_mutex);
        }

        pthread_mutex_unlock(&queue_mutex);

        // Process the connection
        handle_connection(connfd);

        // Close the connection
        close(connfd);
    }

    return NULL;
}

void log_request(const char *operation, const char *uri, uint16_t status_code, const char *request_id) {
    pthread_mutex_lock(&log_mutex);

    // If RequestID is NULL, use "0"
    // const char *request_id_value = (request_id != NULL) ? request_id : "0";
    // printf("Logging request: %s,%s,%d,%s\n", operation, uri, status_code, request_id_value); // Debug print

    // Write the log entry to stderr
    // fprintf(stderr, "%s,%s,%d,%s\n", operation, uri, status_code, request_id_value);

    FILE *log_file = fopen("server_debug_log.txt", "a");
    if (log_file) {
        fprintf(log_file, "%s,%s,%d,%s\n", operation, uri, status_code, request_id ? request_id : "0");
        fclose(log_file);
    }


    pthread_mutex_unlock(&log_mutex);
}

int main(int argc, char **argv) {
    
    if (argc < 2) {
        warnx("wrong arguments: %s port_num", argv[0]);
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Use getopt() to parse the command-line arguments */
    int num_threads = 4;
    int opt;

    // printf("Before getopt...\n"); // Debug print

    while ((opt = getopt(argc, argv, "t:")) != -1) {
        switch (opt) {
            case 't':
                num_threads = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-t num_threads] port\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    // printf("After optind >= argc...\n"); // Debug print

    if (optind >= argc) {
        fprintf(stderr, "Port number is required\n");
        return EXIT_FAILURE;
    }

    char *endptr = NULL;
    size_t port = (size_t) strtoull(argv[optind], &endptr, 10);
    if (endptr && *endptr != '\0') {
        fprintf(stderr, "Invalid Port\n");
        return EXIT_FAILURE;
    }

    // printf("Before port # validation...\n"); // Debug print

    if (port < 1 || port > 65535) {
        fprintf(stderr, "Invalid Port\n");
        return EXIT_FAILURE;
    }

    // printf("Starting server with %d threads on port %zu\n", num_threads, port); // Debug print

    signal(SIGPIPE, SIG_IGN);
    Listener_Socket sock;
    if (listener_init(&sock, port) < 0) {
        fprintf(stderr, "Invalid Port\n");
        return EXIT_FAILURE;
    }

    /* Initialize worker threads, the queue, and other data structures */
    request_queue = queue_new(100);

    worker_threads = malloc(num_threads * sizeof(pthread_t));
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&worker_threads[i], NULL, worker_thread, NULL);
    }

    // Initialize the mutex and condition variable
    pthread_mutex_init(&queue_mutex, NULL);
    pthread_cond_init(&queue_cond, NULL);

    // Initialize the global hashtable for file locks
    file_locks = hash_table_new(100); // Choose an appropriate size for the hashtable

    /* Hint: You will need to change how handle_connection() is used */
    while (1) {
        int connfd = listener_accept(&sock);
        handle_connection(connfd);
        close(connfd);
    }

    return EXIT_SUCCESS;
}

void handle_connection(int connfd) {
    // printf("Handling connection: %d\n", connfd); // Debug print

    conn_t *conn = conn_new(connfd); // Create a new connection
    if(!conn) {
        fprintf(stderr, "Error creating connection for connfd %d\n", connfd);
        return;
    }

    const Response_t *res = conn_parse(conn); // Parse the request
    if (res != NULL) {
        // printf("Parsed response, sending...\n"); // Debug print
        conn_send_response(conn, res);
    }

    else {
        // printf("Request successfully parsed\n"); // Debug print
        const Request_t *req = conn_get_request(conn); // Get the request
        if (req == &REQUEST_GET) {
            // printf("Processing GET request...\n"); // Debug print
            handle_get(conn);
        } 
        
        else if (req == &REQUEST_PUT) {
            // printf("Processing PUT request...\n"); // Debug print
            handle_put(conn);
        } 
        
        else {
            // printf("Processing unsupported request...\n"); // Debug print
            handle_unsupported(conn);
        }
    }

    conn_delete(&conn); // Delete the connection
    // printf("Connection %d handled\n", connfd); // Debug print
}

rwlock_t *file_lock;
void handle_get(conn_t *conn) {
    char *uri = conn_get_uri(conn);
    debug("Handling GET request for %s", uri);
    
    // Get the file lock
    rwlock_t *file_lock = hash_table_get_or_create(file_locks, uri);

    // Acquire the read lock
    reader_lock(file_lock);

    // What are the steps in here?

    // 1. Open the file.
    // If open() returns < 0, then use the result appropriately
    //   a. Cannot access -- use RESPONSE_FORBIDDEN
    //   b. Cannot find the file -- use RESPONSE_NOT_FOUND
    //   c. Other error? -- use RESPONSE_INTERNAL_SERVER_ERROR
    // (Hint: Check errno for these cases)!
    int fd = open(uri, O_RDONLY);
    if (fd < 0) {
        if (errno == EACCES) {
            conn_send_response(conn, &RESPONSE_FORBIDDEN);

            fprintf(stderr, "Logging GET with Forbidden: URI=%s, Status=%d, RequestID=%s\n",
                   uri, response_get_code(&RESPONSE_FORBIDDEN),
                   conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

            log_request("GET", uri, response_get_code(&RESPONSE_FORBIDDEN), conn_get_header(conn, "RequestID"));
        }
        
        else if (errno == ENOENT) {
            conn_send_response(conn, &RESPONSE_NOT_FOUND);

            fprintf(stderr, "Logging GET with Not Found: URI=%s, Status=%d, RequestID=%s\n",
                   uri, response_get_code(&RESPONSE_NOT_FOUND),
                   conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

            log_request("GET", uri, response_get_code(&RESPONSE_NOT_FOUND), conn_get_header(conn, "RequestID"));
        }
        
        else {
            conn_send_response(conn, &RESPONSE_INTERNAL_SERVER_ERROR);

            fprintf(stderr, "Logging GET with Internal Server Error: URI=%s, Status=%d, RequestID=%s\n",
                   uri, response_get_code(&RESPONSE_INTERNAL_SERVER_ERROR),
                   conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

            log_request("GET", uri, response_get_code(&RESPONSE_INTERNAL_SERVER_ERROR), conn_get_header(conn, "RequestID"));
        }

        reader_unlock(file_lock);
        return;
    }

    // 2. Get the size of the file.
    // (Hint: Checkout the function fstat())!
    struct stat st;
    if (fstat(fd, &st) < 0) {
        conn_send_response(conn, &RESPONSE_INTERNAL_SERVER_ERROR);

        fprintf(stderr, "Logging GET with Internal Server Error: URI=%s, Status=%d, RequestID=%s\n",
               uri, response_get_code(&RESPONSE_INTERNAL_SERVER_ERROR),
               conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

        log_request("GET", uri, response_get_code(&RESPONSE_INTERNAL_SERVER_ERROR), conn_get_header(conn, "RequestID"));
        close(fd);
        reader_unlock(file_lock);
        return;
    }

    // 3. Check if the file is a directory, because directories *will*
    // open, but are not valid.
    // (Hint: Checkout the macro "S_IFDIR", which you can use after you call fstat()!)
    if (S_ISDIR(st.st_mode)) {
        conn_send_response(conn, &RESPONSE_FORBIDDEN);

        fprintf(stderr, "Logging GET with Forbidden: URI=%s, Status=%d, RequestID=%s\n",
               uri, response_get_code(&RESPONSE_FORBIDDEN),
               conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

        log_request("GET", uri, response_get_code(&RESPONSE_FORBIDDEN), conn_get_header(conn, "RequestID"));
        reader_unlock(file_lock);
        close(fd);
        return;
    }

    // 4. Send the file
    // (Hint: Checkout the conn_send_file() function!)
    const Response_t *res = conn_send_file(conn, fd, st.st_size);
    if (res) {
        conn_send_response(conn, res);

        fprintf(stderr, "Logging GET with Response: URI=%s, Status=%d, RequestID=%s\n",
               uri, response_get_code(res),
               conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

        log_request("GET", uri, response_get_code(res), conn_get_header(conn, "RequestID"));
    }

    else {

        fprintf(stderr, "Logging GET with OK: URI=%s, Status=%d, RequestID=%s\n",
               uri, response_get_code(&RESPONSE_OK),
               conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

        log_request("GET", uri, response_get_code(&RESPONSE_OK), conn_get_header(conn, "RequestID"));
    }

    // 5. Close the file
    close(fd);

    // Release the read lock
    reader_unlock(file_lock);
}

void handle_put(conn_t *conn) {
    printf("[DEBUG] Entered handle_get()\n"); // Debug log

    char *uri = conn_get_uri(conn);
    const Response_t *res = NULL;
    debug("Handling PUT request for %s", uri);

    // Get the file lock
    rwlock_t *file_lock = hash_table_get_or_create(file_locks, uri);

    // Acquire the write lock
    writer_lock(file_lock);

    // What are the steps in here?

    // 1. Check if file already exists before opening it.
    // (Hint: check the access() function)!
    bool file_exists = (access(uri, F_OK) == 0);

    // 2. Open the file.
    // If open() returns < 0, then use the result appropriately
    //   a. Cannot access -- use RESPONSE_FORBIDDEN
    //   b. File is a directory -- use RESPONSE_FORBIDDEN
    //   c. Cannot find the file -- use RESPONSE_FORBIDDEN
    //   d. Other error? -- use RESPONSE_INTERNAL_SERVER_ERROR
    // (Hint: Check errno for these cases)!
    int fd = open(uri, O_CREAT | O_WRONLY | O_TRUNC, 0664);
    if (fd < 0) {
        if (errno == EACCES) {
            conn_send_response(conn, &RESPONSE_FORBIDDEN);

            fprintf(stderr, "Logging PUT with Forbidden: URI=%s, Status=%d, RequestID=%s\n",
                   uri, response_get_code(&RESPONSE_FORBIDDEN),
                   conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

            log_request("PUT", uri, response_get_code(&RESPONSE_FORBIDDEN), conn_get_header(conn, "RequestID"));
        }

        else if (errno == EISDIR) {
            conn_send_response(conn, &RESPONSE_FORBIDDEN);

            fprintf(stderr, "Logging PUT with Forbidden: URI=%s, Status=%d, RequestID=%s\n",
                   uri, response_get_code(&RESPONSE_FORBIDDEN),
                   conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

            log_request("PUT", uri, response_get_code(&RESPONSE_FORBIDDEN), conn_get_header(conn, "RequestID"));
        }

        else if (errno == ENOENT) {
            conn_send_response(conn, &RESPONSE_FORBIDDEN);

            fprintf(stderr, "Logging PUT with Forbidden: URI=%s, Status=%d, RequestID=%s\n",
                   uri, response_get_code(&RESPONSE_FORBIDDEN),
                   conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

            log_request("PUT", uri, response_get_code(&RESPONSE_FORBIDDEN), conn_get_header(conn, "RequestID"));
        }
        
        else {
            conn_send_response(conn, &RESPONSE_INTERNAL_SERVER_ERROR);

            fprintf(stderr, "Logging PUT with Internal Server Error: URI=%s, Status=%d, RequestID=%s\n",
                   uri, response_get_code(&RESPONSE_INTERNAL_SERVER_ERROR),
                   conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

            log_request("PUT", uri, response_get_code(&RESPONSE_INTERNAL_SERVER_ERROR), conn_get_header(conn, "RequestID"));
        }

        writer_unlock(file_lock);
        return;
    }

    // 3. Receive the file
    // (Hint: Checkout the conn_recv_file() function)!
    res = conn_recv_file(conn, fd);
    if (res != NULL) {
        conn_send_response(conn, res);

        fprintf(stderr, "Logging PUT with Response: URI=%s, Status=%d, RequestID=%s\n",
               uri, response_get_code(res),
               conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

        log_request("PUT", uri, response_get_code(res), conn_get_header(conn, "RequestID"));
        close(fd);
        writer_unlock(file_lock);
        return;
    }

    // 4. Send the response
    // (Hint: Checkout the conn_send_response() function)!
    if (!file_exists) {
        conn_send_response(conn, &RESPONSE_CREATED);

        fprintf(stderr, "Logging PUT with Created: URI=%s, Status=%d, RequestID=%s\n",
               uri, response_get_code(&RESPONSE_CREATED),
               conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

        log_request("PUT", uri, response_get_code(&RESPONSE_CREATED), conn_get_header(conn, "RequestID"));
    } 
    
    else {
        conn_send_response(conn, &RESPONSE_OK);

        fprintf(stderr, "Logging PUT with OK: URI=%s, Status=%d, RequestID=%s\n",
               uri, response_get_code(&RESPONSE_OK),
               conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0"); // Debug print

        log_request("PUT", uri, response_get_code(&RESPONSE_OK), conn_get_header(conn, "RequestID"));
    }

    // 5. Close the file
    close(fd);

    // Release the write lock
    writer_unlock(file_lock);
}

void handle_unsupported(conn_t *conn) {
    char *uri = conn_get_uri(conn);
    // debug("Handling unsupported request");
    debug("Entered handle_unsupported for URI: %s", uri ? uri : "NULL");


    log_request("UNSUPPORTED", uri ? uri : "", response_get_code(&RESPONSE_NOT_IMPLEMENTED), conn_get_header(conn, "RequestID"));

    debug("[DEBUG] After logging UNSUPPORTED request: URI=%s, Status=%d, RequestID=%s",
          uri ? uri : "NULL",
          response_get_code(&RESPONSE_NOT_IMPLEMENTED),
          conn_get_header(conn, "RequestID") ? conn_get_header(conn, "RequestID") : "0");

    // Send responses
    conn_send_response(conn, &RESPONSE_NOT_IMPLEMENTED);
    debug("[DEBUG] Sent RESPONSE_NOT_IMPLEMENTED for URI: %s", uri ? uri : "NULL");
}
