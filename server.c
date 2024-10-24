#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define MAX_LINE_LENGTH 1024
#define MAX_CLIENTS 10

typedef struct Node {
    char line[MAX_LINE_LENGTH];
    struct Node *next;
    struct Node *book_next;
    struct Node *next_frequent_search;
} Node;

typedef struct Book {
    char title[MAX_LINE_LENGTH];
    Node *head;
    Node *tail;
    struct Book *next;
} Book;

// Global variables for shared data
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
Book *book_list = NULL;
int book_count = 0;
char search_term[MAX_LINE_LENGTH] = {0};

// Function to add a line to a specific book in the shared list
void add_line_to_book(Book *book, const char *line) {
    pthread_mutex_lock(&list_mutex);

    Node *new_node = (Node *)malloc(sizeof(Node));
    strncpy(new_node->line, line, MAX_LINE_LENGTH);
    new_node->next = NULL;
    new_node->book_next = NULL;
    new_node->next_frequent_search = NULL;

    if (book->tail) {
        book->tail->book_next = new_node;
    } else {
        book->head = new_node;
    }
    book->tail = new_node;

    pthread_mutex_unlock(&list_mutex);
}

// Function to create or find a book by its title
Book *get_or_create_book(const char *title) {
    pthread_mutex_lock(&list_mutex);

    Book *current = book_list;
    while (current) {
        if (strcmp(current->title, title) == 0) {
            pthread_mutex_unlock(&list_mutex);
            return current;
        }
        current = current->next;
    }

    Book *new_book = (Book *)malloc(sizeof(Book));
    strncpy(new_book->title, title, MAX_LINE_LENGTH);
    new_book->head = NULL;
    new_book->tail = NULL;
    new_book->next = book_list;
    book_list = new_book;

    pthread_mutex_unlock(&list_mutex);
    return new_book;
}

// Function to handle each client
void *handle_client(void *arg) {
    int client_sock = *((int *)arg);
    free(arg);

    char buffer[MAX_LINE_LENGTH];
    ssize_t read_size;
    char book_title[MAX_LINE_LENGTH] = {0};
    int first_line = 1;
    Book *current_book = NULL;

    while ((read_size = recv(client_sock, buffer, MAX_LINE_LENGTH, 0)) > 0) {
        buffer[read_size] = '\0';
        if (first_line) {
            strncpy(book_title, buffer, MAX_LINE_LENGTH);
            current_book = get_or_create_book(book_title);
            printf("Received book title: %s\n", book_title);
            first_line = 0;
        } else {
            add_line_to_book(current_book, buffer);
            printf("Added line to book: %s", buffer);
        }
    }

    if (current_book) {
        // Save the book to a file
        char filename[32];
        sprintf(filename, "book_%02d.txt", ++book_count);
        FILE *file = fopen(filename, "w");
        Node *current_node = current_book->head;
        while (current_node) {
            fprintf(file, "%s", current_node->line);
            current_node = current_node->book_next;
        }
        fclose(file);
        printf("Saved book to %s\n", filename);
    }

    close(client_sock);
    return NULL;
}

// Function to analyze the shared list for search patterns
void *analyze_pattern(void *arg) {
    while (1) {
        sleep(5);  // Analyze every 5 seconds
        pthread_mutex_lock(&list_mutex);

        printf("Analyzing pattern '%s'\n", search_term);
        Book *current_book = book_list;
        while (current_book) {
            int count = 0;
            Node *current_node = current_book->head;
            while (current_node) {
                if (strstr(current_node->line, search_term)) {
                    count++;
                }
                current_node = current_node->book_next;
            }
            printf("Book '%s' has %d occurrences of '%s'.\n", current_book->title, count, search_term);
            current_book = current_book->next;
        }

        pthread_mutex_unlock(&list_mutex);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s -l <port> -p <search_term>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[2]);
    strncpy(search_term, argv[4], MAX_LINE_LENGTH);

    int server_sock, client_sock, *new_sock;
    struct sockaddr_in server, client;
    socklen_t client_len = sizeof(client);
    pthread_t thread_id, analysis_thread;

    // Create socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1) {
        perror("Could not create socket");
        exit(EXIT_FAILURE);
    }

    // Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    // Bind
    if (bind(server_sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Bind failed");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Listen
    listen(server_sock, MAX_CLIENTS);
    printf("Server listening on port %d\n", port);

    // Start analysis thread
    if (pthread_create(&analysis_thread, NULL, analyze_pattern, NULL) < 0) {
        perror("Could not create analysis thread");
        close(server_sock);
        exit(EXIT_FAILURE);
    }

    // Accept and handle incoming connections
    while ((client_sock = accept(server_sock, (struct sockaddr *)&client, &client_len)) >= 0) {
        new_sock = malloc(sizeof(int));
        *new_sock = client_sock;
        if (pthread_create(&thread_id, NULL, handle_client, (void *)new_sock) < 0) {
            perror("Could not create client handler thread");
            close(client_sock);
            free(new_sock);
        }
        pthread_detach(thread_id);
    }

    if (client_sock < 0) {
        perror("Accept failed");
    }

    close(server_sock);
    return 0;
}
