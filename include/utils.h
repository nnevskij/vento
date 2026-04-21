#ifndef UTILS_H
#define UTILS_H

// Reads a file from disk into a memory buffer.
// Returns the content or NULL if the file does not exist.
char* read_file_to_buffer(const char *filepath, long *file_size);

// Given a filename (e.g., "style.css"), returns the correct MIME type
// (e.g., "text/css") for the "Content-Type" HTTP header.
const char* get_mime_type(const char *filepath);

#endif