// Shahriyar Mirzayev 251ADB098
// https://github.com/shehriyar-310/calc-lab-shahriyarm
// Compile with: gcc -O2 -Wall -Wextra -std=c17 -o calc calc.c -lm

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define STUDENT_FIRST_NAME "Shahriyar"
#define STUDENT_LAST_NAME "Mirzayev"
#define STUDENT_ID "251ADB098"

typedef struct {
    long long integer_value;
    double real_value;
    int is_real;
    size_t operand_start;
} CalcValue;

typedef enum {
    TOK_END,
    TOK_NUMBER,
    TOK_PLUS,
    TOK_MINUS,
    TOK_MULTIPLY,
    TOK_DIVIDE,
    TOK_POWER,
    TOK_LEFT_PAREN,
    TOK_RIGHT_PAREN,
    TOK_INVALID
} TokenType;

typedef struct {
    TokenType kind;
    CalcValue value;
    size_t position;
} TokenInfo;

typedef struct {
    const char *line;
    size_t length;
    size_t cursor;
    size_t file_line_start;
    size_t end_position;
    size_t error_position;
    int has_error;
    TokenInfo current_token;
} ParserState;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} DynamicLineBuffer;

static CalcValue parse_expression(ParserState *parser_state);

static CalcValue make_integer(long long number, size_t position)
{
    CalcValue value;
    value.integer_value = number;
    value.real_value = 0.0;
    value.is_real = 0;
    value.operand_start = position;
    return value;
}

static CalcValue make_float(double number, size_t position)
{
    CalcValue value;
    value.integer_value = 0;
    value.real_value = number;
    value.is_real = 1;
    value.operand_start = position;
    return value;
}

static void set_parse_error(ParserState *parser_state, size_t position)
{
    if (!parser_state->has_error) {
        parser_state->has_error = 1;
        parser_state->error_position = position;
    }
}

// Adding the line's absolute start to cursor keeps positions correct even after blank and comment lines earlier in the file

static void next_token(ParserState *parser_state)
{
    TokenInfo current_token;
    size_t start;

    while (parser_state->cursor < parser_state->length &&
           (parser_state->line[parser_state->cursor] == ' ' ||
            parser_state->line[parser_state->cursor] == '\t' ||
            parser_state->line[parser_state->cursor] == '\r')) {
        parser_state->cursor++;
    }

    current_token.kind = TOK_END;
    current_token.position = parser_state->file_line_start + parser_state->cursor;
    current_token.value = make_integer(0, current_token.position);

    if (parser_state->cursor >= parser_state->length) {
        parser_state->current_token = current_token;
        return;
    }

    start = parser_state->cursor;
    current_token.position = parser_state->file_line_start + start;

    if (isdigit((unsigned char)parser_state->line[start])) {
        size_t end;
        size_t dot = (size_t)-1;

        while (parser_state->cursor < parser_state->length &&
               isdigit((unsigned char)parser_state->line[parser_state->cursor])) {
            parser_state->cursor++;
        }
        end = parser_state->cursor;

        if (parser_state->cursor < parser_state->length &&
            parser_state->line[parser_state->cursor] == '.') {
            dot = parser_state->cursor;
            parser_state->cursor++;
            if (parser_state->cursor >= parser_state->length ||
                !isdigit((unsigned char)parser_state->line[parser_state->cursor])) {
                set_parse_error(parser_state, parser_state->file_line_start + dot);
                current_token.kind = TOK_INVALID;
                current_token.position = parser_state->file_line_start + dot;
                parser_state->current_token = current_token;
                return;
            }
            while (parser_state->cursor < parser_state->length &&
                   isdigit((unsigned char)parser_state->line[parser_state->cursor])) {
                parser_state->cursor++;
            }
        }

        if (dot == (size_t)-1) {
            long long number = 0;
            size_t i;

            for (i = start; i < end; i++) {
                number = number * 10 + (parser_state->line[i] - '0');
            }
            current_token.kind = TOK_NUMBER;
            current_token.value = make_integer(number, current_token.position);
        } else {
            double number = 0.0;
            double place = 0.1;
            size_t i;

            for (i = start; i < dot; i++) {
                number = number * 10.0 + (parser_state->line[i] - '0');
            }
            for (i = dot + 1; i < parser_state->cursor; i++) {
                number += (parser_state->line[i] - '0') * place;
                place *= 0.1;
            }
            current_token.kind = TOK_NUMBER;
            current_token.value = make_float(number, current_token.position);
        }

        parser_state->current_token = current_token;
        return;
    }

    parser_state->cursor++;
    switch (parser_state->line[start]) {
        case '+':
            current_token.kind = TOK_PLUS;
            break;
        case '-':
            current_token.kind = TOK_MINUS;
            break;
        case '*':
            if (parser_state->cursor < parser_state->length &&
                parser_state->line[parser_state->cursor] == '*') {
                parser_state->cursor++;
                current_token.kind = TOK_POWER;
            } else {
                current_token.kind = TOK_MULTIPLY;
            }
            break;
        case '/':
            current_token.kind = TOK_DIVIDE;
            break;
        case '(':
            current_token.kind = TOK_LEFT_PAREN;
            break;
        case ')':
            current_token.kind = TOK_RIGHT_PAREN;
            break;
        default:
            current_token.kind = TOK_INVALID;
            set_parse_error(parser_state, current_token.position);
            break;
    }
    parser_state->current_token = current_token;
}

static void advance_token(ParserState *parser_state)
{
    next_token(parser_state);
}

static size_t unexpected_position(const ParserState *parser_state)
{
//At line end, report the newline or one past EOF; otherwise report the token that stopped parsing. This also locates missing operands.
    return parser_state->current_token.kind == TOK_END
               ? parser_state->end_position
               : parser_state->current_token.position;
}

static CalcValue parse_atom(ParserState *parser_state)
{
    TokenInfo current_token = parser_state->current_token;

    if (parser_state->has_error) {
        return make_integer(0, parser_state->error_position);
    }

    if (current_token.kind == TOK_NUMBER) {
        advance_token(parser_state);
        return current_token.value;
    }

    if (current_token.kind == TOK_LEFT_PAREN) {
        size_t opening_position = current_token.position;
        CalcValue value;

        advance_token(parser_state);
        value = parse_expression(parser_state);
        if (parser_state->has_error) {
            return value;
        }

        if (parser_state->current_token.kind != TOK_RIGHT_PAREN) {
            set_parse_error(parser_state, parser_state->end_position);
            return value;
        }

        advance_token(parser_state);
        value.operand_start = opening_position;
        return value;
    }

    set_parse_error(parser_state, unexpected_position(parser_state));
    return make_integer(0, unexpected_position(parser_state));
}

static double as_double(CalcValue value)
{
    return value.is_real ? value.real_value : (double)value.integer_value;
}

static CalcValue evaluate_operator(ParserState *parser_state, TokenType operation,
                            size_t operator_position, CalcValue left, CalcValue right)
{
    CalcValue result;

    if (operation == TOK_DIVIDE) {
        int zero = right.is_real ? right.real_value == 0.0
                                  : right.integer_value == 0;
        if (zero) {
            set_parse_error(parser_state, right.operand_start);
            return make_integer(0, left.operand_start);
        }
    }

    if (operation == TOK_POWER && !left.is_real && !right.is_real &&
        right.integer_value < 0) {
        set_parse_error(parser_state, right.operand_start);
        return make_integer(0, left.operand_start);
    }

    if (left.is_real || right.is_real) {
        double a = as_double(left);
        double b = as_double(right);
        double evaluation_result;

        switch (operation) {
            case TOK_PLUS:
                evaluation_result = a + b;
                break;
            case TOK_MINUS:
                evaluation_result = a - b;
                break;
            case TOK_MULTIPLY:
                evaluation_result = a * b;
                break;
            case TOK_DIVIDE:
                evaluation_result = a / b;
                break;
            case TOK_POWER:
                evaluation_result = pow(a, b);
                break;
            default:
                set_parse_error(parser_state, operator_position);
                return make_float(0.0, left.operand_start);
        }

        if (!isfinite(evaluation_result)) {
            set_parse_error(parser_state, operator_position);
            return make_float(0.0, left.operand_start);
        }
        return make_float(evaluation_result, left.operand_start);
    }

    result = make_integer(0, left.operand_start);
    switch (operation) {
        case TOK_PLUS:
            result.integer_value = left.integer_value + right.integer_value;
            break;
        case TOK_MINUS:
            result.integer_value = left.integer_value - right.integer_value;
            break;
        case TOK_MULTIPLY:
            result.integer_value = left.integer_value * right.integer_value;
            break;
        case TOK_DIVIDE:
            result.integer_value = left.integer_value / right.integer_value;
            break;
        case TOK_POWER: {
            long long factor = left.integer_value;
            long long exponent = right.integer_value;
            long long product = 1;

            while (exponent > 0) {
                if (exponent % 2 != 0) {
                    product *= factor;
                }
                exponent /= 2;
                if (exponent > 0) {
                    factor *= factor;
                }
            }
            result.integer_value = product;
            break;
        }
        default:
            set_parse_error(parser_state, operator_position);
            break;
    }
    return result;
}

static CalcValue parse_exponent(ParserState *parser_state)
{
    CalcValue left = parse_atom(parser_state);

    if (parser_state->has_error || parser_state->current_token.kind != TOK_POWER) {
        return left;
    }

    {
        size_t operator_position = parser_state->current_token.position;
        CalcValue right;

        advance_token(parser_state);
        right = parse_exponent(parser_state);
        if (parser_state->has_error) {
            return left;
        }
        return evaluate_operator(parser_state, TOK_POWER, operator_position, left, right);
    }
}

static CalcValue parse_term(ParserState *parser_state)
{
    CalcValue left = parse_exponent(parser_state);

    while (!parser_state->has_error &&
           (parser_state->current_token.kind == TOK_MULTIPLY ||
            parser_state->current_token.kind == TOK_DIVIDE)) {
        TokenInfo operation = parser_state->current_token;
        CalcValue right;

        advance_token(parser_state);
        right = parse_exponent(parser_state);
        if (parser_state->has_error) {
            return left;
        }
        left = evaluate_operator(parser_state, operation.kind, operation.position,
                              left, right);
    }
    return left;
}

static CalcValue parse_expression(ParserState *parser_state)
{
    CalcValue left = parse_term(parser_state);

    while (!parser_state->has_error &&
           (parser_state->current_token.kind == TOK_PLUS ||
            parser_state->current_token.kind == TOK_MINUS)) {
        TokenInfo operation = parser_state->current_token;
        CalcValue right;

        advance_token(parser_state);
        right = parse_term(parser_state);
        if (parser_state->has_error) {
            return left;
        }
        left = evaluate_operator(parser_state, operation.kind, operation.position,
                              left, right);
    }
    return left;
}

static int should_skip_line(const char *line, size_t length)
{
    size_t i = 0;

    while (i < length &&
           (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) {
        i++;
    }
    return i == length || line[i] == '#';
}

static int evaluate_line(const char *line, size_t length, size_t file_line_start,
                         CalcValue *evaluation_result, size_t *error_position)
{
    ParserState parser_state;

    if (should_skip_line(line, length)) {
        return 0;
    }

    parser_state.line = line;
    parser_state.length = length;
    parser_state.cursor = 0;
    parser_state.file_line_start = file_line_start;
    parser_state.end_position = file_line_start + length;
    parser_state.error_position = 0;
    parser_state.has_error = 0;
    next_token(&parser_state);

    *evaluation_result = parse_expression(&parser_state);
    if (!parser_state.has_error && parser_state.current_token.kind != TOK_END) {
        /* Any token left after a complete expression is unexpected; point to it. */
        set_parse_error(&parser_state, parser_state.current_token.position);
    }

    if (parser_state.has_error) {
        *error_position = parser_state.error_position;
        return -1;
    }
    return 1;
}

// Doubling capacity keeps long-line reading linear overall instead of copying all previously read characters again each time the buffer fills.
static int ensure_buffer_capacity(DynamicLineBuffer *line, size_t needed_characters)
{
    size_t needed = needed_characters + 1;
    size_t capacity;
    char *grown;

    if (line->capacity >= needed) {
        return 1;
    }

    capacity = line->capacity == 0 ? 256 : line->capacity;
    while (capacity < needed) {
        if (capacity > (size_t)-1 / 2) {
            return 0;
        }
        capacity *= 2;
    }

    grown = realloc(line->data, capacity);
    if (grown == NULL) {
        return 0;
    }
    line->data = grown;
    line->capacity = capacity;
    return 1;
}

static int read_line(FILE *input, DynamicLineBuffer *line, size_t *length,
                     int *has_newline)
{
    int character;
    int saw_character = 0;

    line->length = 0;
    *has_newline = 0;
    if (!ensure_buffer_capacity(line, 0)) {
        return -1;
    }

    while ((character = fgetc(input)) != EOF) {
        saw_character = 1;
        if (character == '\n') {
            *has_newline = 1;
            line->data[line->length] = '\0';
            *length = line->length;
            return 1;
        }

        if (!ensure_buffer_capacity(line, line->length + 1)) {
            return -1;
        }
        line->data[line->length++] = (char)character;
    }

    if (ferror(input)) {
        return -1;
    }
    if (!saw_character) {
        return 0;
    }

    line->data[line->length] = '\0';
    *length = line->length;
    return 1;
}

static int write_value(FILE *output, CalcValue value)
{
    int written;

    if (value.is_real) {
        written = fprintf(output, "%.15g\n", value.real_value);
    } else {
        written = fprintf(output, "%lld\n", value.integer_value);
    }
    return written >= 0;
}

static char *duplicate_n(const char *text, size_t length)
{
    char *copy = malloc(length + 1);

    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

static char *join_paths(const char *directory, const char *name)
{
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    int needs_slash = directory_length > 0 &&
                      directory[directory_length - 1] != '/';
    size_t total = directory_length + (size_t)needs_slash + name_length + 1;
    char *path = malloc(total);

    if (path == NULL) {
        return NULL;
    }
    memcpy(path, directory, directory_length);
    if (needs_slash) {
        path[directory_length++] = '/';
    }
    memcpy(path + directory_length, name, name_length + 1);
    return path;
}

static char *get_path_basename(const char *path, int remove_txt_suffix)
{
    size_t length = strlen(path);
    size_t start;
    size_t base_length;

    while (length > 1 && path[length - 1] == '/') {
        length--;
    }
    start = length;
    while (start > 0 && path[start - 1] != '/') {
        start--;
    }
    base_length = length - start;

    if (remove_txt_suffix && base_length >= 4 &&
        memcmp(path + length - 4, ".txt", 4) == 0) {
        base_length -= 4;
    }
    return duplicate_n(path + start, base_length);
}

static char *sanitize_filename_part(const char *text)
{
    size_t length = strlen(text);
    char *result = malloc(length + 2);
    size_t i;
    size_t used = 0;

    if (result == NULL) {
        return NULL;
    }

    for (i = 0; i < length; i++) {
        unsigned char character = (unsigned char)text[i];

        if (isalnum(character)) {
            result[used++] = (char)tolower(character);
        } else if (used > 0 && result[used - 1] != '_') {
            result[used++] = '_';
        }
    }
    while (used > 0 && result[used - 1] == '_') {
        used--;
    }
    if (used == 0) {
        memcpy(result, "student", 7);
        used = 7;
    }
    result[used] = '\0';
    return result;
}

static char *make_default_directory(const char *base)
{
    char *first = sanitize_filename_part(STUDENT_FIRST_NAME);
    char *identifier = sanitize_filename_part(STUDENT_ID);
    size_t needed;
    char *result;

    if (first == NULL || identifier == NULL) {
        free(first);
        free(identifier);
        return NULL;
    }

    needed = strlen(base) + strlen(first) + strlen(identifier) + 3;
    result = malloc(needed);
    if (result != NULL) {
        snprintf(result, needed, "%s_%s_%s", base, first, identifier);
    }
    free(first);
    free(identifier);
    return result;
}

static char *make_output_filename(const char *base)
{
    char *first = sanitize_filename_part(STUDENT_FIRST_NAME);
    char *last = sanitize_filename_part(STUDENT_LAST_NAME);
    char *identifier = sanitize_filename_part(STUDENT_ID);
    size_t needed;
    char *result;

    if (first == NULL || last == NULL || identifier == NULL) {
        free(first);
        free(last);
        free(identifier);
        return NULL;
    }

    needed = strlen(base) + strlen(first) + strlen(last) +
             strlen(identifier) + 8;
    result = malloc(needed);
    if (result != NULL) {
        snprintf(result, needed, "%s_%s_%s_%s.txt",
                 base, first, last, identifier);
    }
    free(first);
    free(last);
    free(identifier);
    return result;
}

static int ensure_directory(const char *path)
{
    struct stat status;

    if (stat(path, &status) == 0) {
        if (S_ISDIR(status.st_mode)) {
            return 1;
        }
        fprintf(stderr, "calc: output path is not a directory: %s\n", path);
        return 0;
    }

    if (errno != ENOENT || mkdir(path, 0777) != 0) {
        fprintf(stderr, "calc: cannot create output directory %s: %s\n",
                path, strerror(errno));
        return 0;
    }
    return 1;
}

static int process_file(const char *input_path, const char *output_dir)
{
    FILE *input = fopen(input_path, "rb");
    char *base = NULL;
    char *filename = NULL;
    char *output_path = NULL;
    char *temporary_path = NULL;
    FILE *output = NULL;
    DynamicLineBuffer line = {NULL, 0, 0};
    size_t file_line_start = 1;
    size_t error_position = 0;
    int parse_error = 0;
    int success = 1;
    int read_status;
    size_t length;
    int has_newline;

    if (input == NULL) {
        fprintf(stderr, "calc: cannot open input %s: %s\n",
                input_path, strerror(errno));
        return 0;
    }

    base = get_path_basename(input_path, 1);
    if (base != NULL) {
        filename = make_output_filename(base);
    }
    if (filename != NULL) {
        output_path = join_paths(output_dir, filename);
    }
    if (output_path != NULL) {
        size_t length = strlen(output_path);
        temporary_path = malloc(length + 5);
        if (temporary_path != NULL) {
            snprintf(temporary_path, length + 5, "%s.tmp", output_path);
        }
    }

    if (temporary_path == NULL) {
        fprintf(stderr, "calc: out of memory while creating output path\n");
        success = 0;
        goto finish;
    }

    output = fopen(temporary_path, "w");
    if (output == NULL) {
        fprintf(stderr, "calc: cannot write output %s: %s\n",
                output_path, strerror(errno));
        success = 0;
        goto finish;
    }

    while ((read_status = read_line(input, &line, &length,
                                    &has_newline)) > 0) {
        CalcValue evaluation_result;
        int line_status;

        line_status = evaluate_line(line.data, length, file_line_start,
                                    &evaluation_result, &error_position);
        if (line_status < 0) {
            parse_error = 1;
            break;
        }
        if (line_status > 0 && !write_value(output, evaluation_result)) {
            fprintf(stderr, "calc: has_error while writing %s\n", output_path);
            success = 0;
            break;
        }
        file_line_start += length + (has_newline ? 1 : 0);
    }

    if (read_status < 0) {
        fprintf(stderr, "calc: has_error while reading %s\n", input_path);
        success = 0;
    }
    if (fclose(input) != 0) {
        fprintf(stderr, "calc: has_error while closing input %s\n", input_path);
        success = 0;
    }
    input = NULL;

    if (success && parse_error) {
        if (fclose(output) != 0) {
            fprintf(stderr, "calc: has_error while preparing error output %s\n",
                    output_path);
            output = NULL;
            success = 0;
        } else {
            output = fopen(temporary_path, "w");
            if (output == NULL) {
                fprintf(stderr, "calc: cannot write output %s: %s\n",
                        output_path, strerror(errno));
                success = 0;
            } else if (fprintf(output, "ERROR:%zu\n", error_position) < 0) {
                fprintf(stderr, "calc: has_error while writing %s\n", output_path);
                success = 0;
            }
        }
    }

    if (output != NULL) {
        if (fclose(output) != 0) {
            fprintf(stderr, "calc: has_error while closing output %s\n",
                    output_path);
            success = 0;
        }
        output = NULL;
    }

    if (success && rename(temporary_path, output_path) != 0) {
        fprintf(stderr, "calc: cannot finish output %s: %s\n",
                output_path, strerror(errno));
        success = 0;
    }

finish:
    if (output != NULL) {
        fclose(output);
    }
    if (input != NULL) {
        fclose(input);
    }
    if (!success && temporary_path != NULL) {
        remove(temporary_path);
    }
    free(line.data);
    free(base);
    free(filename);
    free(output_path);
    free(temporary_path);
    return success;
}

static int has_txt_suffix(const char *name)
{
    size_t length = strlen(name);
    return length >= 4 && strcmp(name + length - 4, ".txt") == 0;
}

static int compare_filenames(const void *left, const void *right)
{
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

static int append_filename(char ***names, size_t *count, size_t *capacity,
                    const char *name)
{
    char **grown;
    char *copy;

    if (*count == *capacity) {
        size_t new_capacity = *capacity == 0 ? 8 : *capacity * 2;
        if (new_capacity < *capacity ||
            new_capacity > (size_t)-1 / sizeof(**names)) {
            return 0;
        }
        grown = realloc(*names, new_capacity * sizeof(**names));
        if (grown == NULL) {
            return 0;
        }
        *names = grown;
        *capacity = new_capacity;
    }

    copy = duplicate_n(name, strlen(name));
    if (copy == NULL) {
        return 0;
    }
    (*names)[(*count)++] = copy;
    return 1;
}

static void free_names(char **names, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        free(names[i]);
    }
    free(names);
}

static int evaluate_directory(const char *directory,
                                   const char *output_dir)
{
    DIR *stream = opendir(directory);
    char **names = NULL;
    size_t count = 0;
    size_t capacity = 0;
    struct dirent *entry;
    int success = 1;
    size_t i;

    if (stream == NULL) {
        fprintf(stderr, "calc: cannot open directory %s: %s\n",
                directory, strerror(errno));
        return 0;
    }

    while ((entry = readdir(stream)) != NULL) {
        char *full_path;
        struct stat status;

        if (!has_txt_suffix(entry->d_name)) {
            continue;
        }

        full_path = join_paths(directory, entry->d_name);
        if (full_path == NULL) {
            fprintf(stderr, "calc: out of memory while reading directory\n");
            success = 0;
            break;
        }
        if (stat(full_path, &status) != 0) {
            fprintf(stderr, "calc: cannot inspect %s: %s\n",
                    full_path, strerror(errno));
            free(full_path);
            success = 0;
            break;
        }
        free(full_path);

        if (S_ISREG(status.st_mode) &&
            !append_filename(&names, &count, &capacity, entry->d_name)) {
            fprintf(stderr, "calc: out of memory while listing directory\n");
            success = 0;
            break;
        }
    }

    if (closedir(stream) != 0) {
        fprintf(stderr, "calc: cannot close directory %s: %s\n",
                directory, strerror(errno));
        success = 0;
    }

    if (count > 1) {
        qsort(names, count, sizeof(*names), compare_filenames);
    }
    for (i = 0; i < count; i++) {
        char *full_path = join_paths(directory, names[i]);

        if (full_path == NULL) {
            fprintf(stderr, "calc: out of memory while building input path\n");
            success = 0;
            break;
        }
        if (!process_file(full_path, output_dir)) {
            success = 0;
        }
        free(full_path);
    }

    free_names(names, count);
    return success;
}

static void print_usage(FILE *stream)
{
    fprintf(stream,
            "Usage: calc [-d DIR | --dir DIR] "
            "[-o OUTDIR | --output-dir OUTDIR] [input.txt]\n");
}

int main(int argc, char **argv)
{
    const char *directory = NULL;
    const char *input_path = NULL;
    const char *output_dir_argument = NULL;
    char *default_dir = NULL;
    char *base = NULL;
    int i;
    int success;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--dir") == 0) {
            if (directory != NULL || i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            directory = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 ||
                   strcmp(argv[i], "--output-dir") == 0) {
            if (output_dir_argument != NULL || i + 1 >= argc) {
                print_usage(stderr);
                return 1;
            }
            output_dir_argument = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "calc: unknown option: %s\n", argv[i]);
            print_usage(stderr);
            return 1;
        } else if (input_path == NULL) {
            input_path = argv[i];
        } else {
            fprintf(stderr, "calc: too many input paths\n");
            print_usage(stderr);
            return 1;
        }
    }

    if ((directory == NULL && input_path == NULL) ||
        (directory != NULL && input_path != NULL)) {
        print_usage(stderr);
        return 1;
    }

    if (output_dir_argument == NULL) {
        base = get_path_basename(directory != NULL ? directory : input_path,
                         directory == NULL);
        if (base != NULL) {
            default_dir = make_default_directory(base);
        }
        if (default_dir == NULL) {
            fprintf(stderr, "calc: out of memory while creating output directory name\n");
            free(base);
            return 1;
        }
        output_dir_argument = default_dir;
    }

    if (!ensure_directory(output_dir_argument)) {
        free(base);
        free(default_dir);
        return 1;
    }

    if (directory != NULL) {
        success = evaluate_directory(directory, output_dir_argument);
    } else {
        success = process_file(input_path, output_dir_argument);
    }

    free(base);
    free(default_dir);
    return success ? 0 : 1;
}
