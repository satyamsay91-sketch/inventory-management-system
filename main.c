#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

#define FILE_NAME "inventory.dat"
#define TEMP_FILE "temp.dat"
#define LOG_FILE "transactions.txt"
#define REPORT_FILE "inventory_report.csv"
#define LOW_STOCK 5

struct Item {
    int id;
    char name[50];
    char category[30];
    int qty;
    float price;
};

/* Safe console input: reads a whole line and validates numbers. */
int readLine(const char *prompt, char *buffer, size_t size) {
    size_t len;
    int ch;

    printf("%s", prompt);
    if (fgets(buffer, (int)size, stdin) == NULL) return 0;

    len = strlen(buffer);
    if (len && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    } else {
        while ((ch = getchar()) != '\n' && ch != EOF) { }
    }
    return 1;
}

int readInt(const char *prompt, int min, int max, int *out) {
    char line[128], *end;
    long value;

    for (;;) {
        if (!readLine(prompt, line, sizeof(line))) return 0;
        errno = 0;
        value = strtol(line, &end, 10);
        while (*end == ' ' || *end == '\t') end++;

        if (line[0] && *end == '\0' && errno != ERANGE &&
            value >= min && value <= max) {
            *out = (int)value;
            return 1;
        }
        printf("Invalid input. Enter a whole number from %d to %d.\n", min, max);
    }
}

int readPrice(const char *prompt, float *out) {
    char line[128], *end;
    float value;

    for (;;) {
        if (!readLine(prompt, line, sizeof(line))) return 0;
        errno = 0;
        value = strtof(line, &end);
        while (*end == ' ' || *end == '\t') end++;

        if (line[0] && *end == '\0' && errno != ERANGE &&
            value >= 0.0f && value <= 100000000.0f) {
            *out = value;
            return 1;
        }
        printf("Invalid price. Enter a value from 0 to 100000000.\n");
    }
}

int readText(const char *prompt, char *out, size_t size) {
    for (;;) {
        if (!readLine(prompt, out, size)) return 0;
        if (out[0] != '\0') return 1;
        printf("This field cannot be empty.\n");
    }
}

int findItem(int id, struct Item *result) {
    FILE *fp = fopen(FILE_NAME, "rb");
    struct Item it;

    if (!fp) return 0;
    while (fread(&it, sizeof(it), 1, fp) == 1) {
        if (it.id == id) {
            if (result) *result = it;
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

int containsIgnoreCase(const char *text, const char *query) {
    size_t i, j;
    for (i = 0; text[i]; i++) {
        for (j = 0; query[j]; j++) {
            char a = text[i + j], b = query[j];
            if (!a) break;
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (query[j] == '\0') return 1;
    }
    return query[0] == '\0';
}

void logTransaction(const char *action, const struct Item *it, int change) {
    FILE *fp = fopen(LOG_FILE, "a");
    time_t now;
    struct tm *tmInfo;
    char stamp[30];

    if (!fp) {
        printf("Warning: could not write transaction log.\n");
        return;
    }
    now = time(NULL);
    tmInfo = localtime(&now);
    if (tmInfo) strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", tmInfo);
    else strcpy(stamp, "Unknown time");

    fprintf(fp, "%s | %s | ID %d | %s | Change %+d | Balance %d\n",
            stamp, action, it->id, it->name, change, it->qty);
    fclose(fp);
}

void printHeader(void) {
    printf("\n%-6s %-20s %-15s %-8s %-12s\n",
           "ID", "Name", "Category", "Qty", "Price (Rs)");
    printf("--------------------------------------------------------------------\n");
}

void printItem(const struct Item *it) {
    printf("%-6d %-20.20s %-15.15s %-8d %-12.2f",
           it->id, it->name, it->category, it->qty, it->price);
    if (it->qty <= LOW_STOCK) printf("  <-- LOW STOCK");
    printf("\n");
}

void addItem(void) {
    struct Item it, old;
    FILE *fp;

    printf("\n--- ADD PRODUCT ---\n");
    if (!readInt("Product ID: ", 1, INT_MAX, &it.id)) return;
    if (findItem(it.id, &old)) {
        printf("That ID already exists.\n");
        return;
    }
    if (!readText("Product name: ", it.name, sizeof(it.name))) return;
    if (!readText("Category: ", it.category, sizeof(it.category))) return;
    if (!readInt("Opening quantity: ", 0, 1000000, &it.qty)) return;
    if (!readPrice("Price per item (Rs): ", &it.price)) return;

    fp = fopen(FILE_NAME, "ab");
    if (!fp) {
        printf("Could not open inventory file.\n");
        return;
    }
    if (fwrite(&it, sizeof(it), 1, fp) == 1) {
        printf("Product added successfully.\n");
        logTransaction("NEW PRODUCT", &it, it.qty);
    } else {
        printf("Error saving product.\n");
    }
    fclose(fp);
}

void viewItems(void) {
    struct Item it;
    FILE *fp = fopen(FILE_NAME, "rb");
    int count = 0;
    long units = 0;
    double value = 0;

    printf("\n--- ALL PRODUCTS ---\n");
    if (!fp) {
        printf("No inventory file found. Add a product first.\n");
        return;
    }
    printHeader();
    while (fread(&it, sizeof(it), 1, fp) == 1) {
        printItem(&it);
        count++;
        units += it.qty;
        value += (double)it.qty * it.price;
    }
    if (!count) printf("No products found.\n");
    else {
        printf("\nProduct records: %d\nTotal units: %ld\nStock value: Rs %.2f\n",
               count, units, value);
    }
    if (ferror(fp)) printf("Warning: error reading inventory file.\n");
    fclose(fp);
}

void searchItem(void) {
    struct Item it;
    FILE *fp;
    int choice, id, found = 0;
    char query[50];

    printf("\n1. Search by ID\n2. Search by name\n");
    if (!readInt("Choice: ", 1, 2, &choice)) return;
    fp = fopen(FILE_NAME, "rb");
    if (!fp) {
        printf("No inventory file found.\n");
        return;
    }

    if (choice == 1) {
        if (!readInt("Product ID: ", 1, INT_MAX, &id)) {
            fclose(fp);
            return;
        }
    } else if (!readText("Name or part of name: ", query, sizeof(query))) {
        fclose(fp);
        return;
    }

    while (fread(&it, sizeof(it), 1, fp) == 1) {
        if ((choice == 1 && it.id == id) ||
            (choice == 2 && containsIgnoreCase(it.name, query))) {
            if (!found) printHeader();
            printItem(&it);
            found = 1;
        }
    }
    if (!found) printf("No matching product found.\n");
    fclose(fp);
}

void updateItem(void) {
    struct Item it;
    FILE *fp;
    int id, found = 0;

    printf("\n--- UPDATE PRODUCT ---\n");
    if (!readInt("Product ID: ", 1, INT_MAX, &id)) return;
    fp = fopen(FILE_NAME, "rb+");
    if (!fp) {
        printf("No inventory file found.\n");
        return;
    }

    while (fread(&it, sizeof(it), 1, fp) == 1) {
        if (it.id == id) {
            printf("Current product:\n");
            printHeader();
            printItem(&it);
            if (!readText("New name: ", it.name, sizeof(it.name)) ||
                !readText("New category: ", it.category, sizeof(it.category)) ||
                !readInt("New quantity: ", 0, 1000000, &it.qty) ||
                !readPrice("New price (Rs): ", &it.price)) {
                fclose(fp);
                return;
            }
            if (fseek(fp, -(long)sizeof(it), SEEK_CUR) == 0 &&
                fwrite(&it, sizeof(it), 1, fp) == 1) {
                printf("Product updated.\n");
                logTransaction("PRODUCT UPDATED", &it, 0);
            } else printf("Error updating product.\n");
            found = 1;
            break;
        }
    }
    if (!found) printf("Product ID not found.\n");
    fclose(fp);
}

void deleteItem(void) {
    struct Item it, deleted;
    FILE *fp, *tmp;
    int id, found = 0, error = 0;

    printf("\n--- DELETE PRODUCT ---\n");
    if (!readInt("Product ID: ", 1, INT_MAX, &id)) return;
    fp = fopen(FILE_NAME, "rb");
    if (!fp) {
        printf("No inventory file found.\n");
        return;
    }
    tmp = fopen(TEMP_FILE, "wb");
    if (!tmp) {
        printf("Could not create temporary file.\n");
        fclose(fp);
        return;
    }

    while (fread(&it, sizeof(it), 1, fp) == 1) {
        if (it.id == id) {
            deleted = it;
            found = 1;
        } else if (fwrite(&it, sizeof(it), 1, tmp) != 1) {
            error = 1;
            break;
        }
    }
    if (ferror(fp)) error = 1;
    fclose(fp);
    if (fclose(tmp) != 0) error = 1;

    if (error) {
        remove(TEMP_FILE);
        printf("File error. Original inventory was not changed.\n");
        return;
    }
    if (!found) {
        remove(TEMP_FILE);
        printf("Product ID not found.\n");
        return;
    }
    if (remove(FILE_NAME) != 0 || rename(TEMP_FILE, FILE_NAME) != 0) {
        printf("Could not replace inventory file. Check %s before trying again.\n",
               TEMP_FILE);
        return;
    }
    printf("Product deleted.\n");
    logTransaction("PRODUCT DELETED", &deleted, -deleted.qty);
}

void stockIn(void) {
    struct Item it;
    FILE *fp;
    int id, amount, found = 0;

    printf("\n--- STOCK IN ---\n");
    if (!readInt("Product ID: ", 1, INT_MAX, &id) ||
        !readInt("Quantity received: ", 1, 1000000, &amount)) return;

    fp = fopen(FILE_NAME, "rb+");
    if (!fp) {
        printf("No inventory file found.\n");
        return;
    }
    while (fread(&it, sizeof(it), 1, fp) == 1) {
        if (it.id == id) {
            found = 1;
            if (it.qty > 1000000 - amount) {
                printf("Quantity limit would be exceeded.\n");
            } else {
                it.qty += amount;
                if (fseek(fp, -(long)sizeof(it), SEEK_CUR) == 0 &&
                    fwrite(&it, sizeof(it), 1, fp) == 1) {
                    printf("Stock added. New quantity: %d\n", it.qty);
                    logTransaction("STOCK IN", &it, amount);
                } else printf("Error updating stock.\n");
            }
            break;
        }
    }
    if (!found) printf("Product ID not found.\n");
    fclose(fp);
}

void stockOut(void) {
    struct Item it;
    FILE *fp;
    int id, amount, found = 0;

    printf("\n--- STOCK OUT ---\n");
    if (!readInt("Product ID: ", 1, INT_MAX, &id) ||
        !readInt("Quantity issued: ", 1, 1000000, &amount)) return;

    fp = fopen(FILE_NAME, "rb+");
    if (!fp) {
        printf("No inventory file found.\n");
        return;
    }
    while (fread(&it, sizeof(it), 1, fp) == 1) {
        if (it.id == id) {
            found = 1;
            if (amount > it.qty) {
                printf("Insufficient stock. Available: %d\n", it.qty);
            } else {
                it.qty -= amount;
                if (fseek(fp, -(long)sizeof(it), SEEK_CUR) == 0 &&
                    fwrite(&it, sizeof(it), 1, fp) == 1) {
                    printf("Stock issued. New quantity: %d\n", it.qty);
                    logTransaction("STOCK OUT", &it, -amount);
                } else printf("Error updating stock.\n");
            }
            break;
        }
    }
    if (!found) printf("Product ID not found.\n");
    fclose(fp);
}

void lowStockAlerts(void) {
    struct Item it;
    FILE *fp = fopen(FILE_NAME, "rb");
    int found = 0;

    printf("\n--- LOW STOCK (quantity <= %d) ---\n", LOW_STOCK);
    if (!fp) {
        printf("No inventory file found.\n");
        return;
    }
    while (fread(&it, sizeof(it), 1, fp) == 1) {
        if (it.qty <= LOW_STOCK) {
            if (!found) printHeader();
            printItem(&it);
            found = 1;
        }
    }
    if (!found) printf("No low-stock products.\n");
    fclose(fp);
}

void dashboard(void) {
    struct Item it;
    FILE *fp = fopen(FILE_NAME, "rb");
    int products = 0, low = 0;
    long units = 0;
    double value = 0;

    if (fp) {
        while (fread(&it, sizeof(it), 1, fp) == 1) {
            products++;
            units += it.qty;
            value += (double)it.qty * it.price;
            if (it.qty <= LOW_STOCK) low++;
        }
        fclose(fp);
    }

    printf("\n========== INVENTORY DASHBOARD ==========\n");
    printf("Total products       : %d\n", products);
    printf("Total units          : %ld\n", units);
    printf("Products low in stock: %d\n", low);
    printf("Total stock value    : Rs %.2f\n", value);
    printf("Low-stock limit      : %d units or fewer\n", LOW_STOCK);
    printf("=========================================\n");
}

void writeCsvField(FILE *fp, const char *text) {
    const char *p;
    fputc('"', fp);
    for (p = text; *p; p++) {
        if (*p == '"') fputc('"', fp);
        fputc(*p, fp);
    }
    fputc('"', fp);
}

void generateReport(void) {
    struct Item it;
    FILE *fp = fopen(FILE_NAME, "rb");
    FILE *out;
    int count = 0;
    double total = 0;

    if (!fp) {
        printf("No inventory file found.\n");
        return;
    }
    out = fopen(REPORT_FILE, "w");
    if (!out) {
        printf("Could not create report file.\n");
        fclose(fp);
        return;
    }

    fprintf(out, "ID,Name,Category,Quantity,Unit Price,Stock Value\n");
    while (fread(&it, sizeof(it), 1, fp) == 1) {
        double stockValue = (double)it.qty * it.price;
        fprintf(out, "%d,", it.id);
        writeCsvField(out, it.name);
        fputc(',', out);
        writeCsvField(out, it.category);
        fprintf(out, ",%d,%.2f,%.2f\n", it.qty, it.price, stockValue);
        total += stockValue;
        count++;
    }

    fclose(fp);
    if (fclose(out) == 0)
        printf("Report saved as %s (%d products, total value Rs %.2f).\n",
               REPORT_FILE, count, total);
    else
        printf("Error saving report.\n");
}

void viewTransactionLog(void) {
    char line[256];
    FILE *fp = fopen(LOG_FILE, "r");

    printf("\n--- TRANSACTION HISTORY ---\n");
    if (!fp) {
        printf("No transaction history yet.\n");
        return;
    }
    while (fgets(line, sizeof(line), fp)) printf("%s", line);
    fclose(fp);
}

int main(void) {
    int choice;

    for (;;) {
        printf("\n============================================\n");
        printf("       INVENTORY MANAGEMENT SYSTEM\n");
        printf("============================================\n");
        printf("1.  Add New Product\n");
        printf("2.  View All Products\n");
        printf("3.  Search Product (ID or name)\n");
        printf("4.  Update Product Details\n");
        printf("5.  Delete Product\n");
        printf("6.  Stock In\n");
        printf("7.  Stock Out\n");
        printf("8.  Low Stock Alerts\n");
        printf("9.  Inventory Dashboard\n");
        printf("10. Generate CSV Report\n");
        printf("11. View Transaction History\n");
        printf("12. Exit\n");
        printf("============================================\n");

        if (!readInt("Enter choice: ", 1, 12, &choice)) {
            printf("\nInput ended. Goodbye!\n");
            break;
        }

        switch (choice) {
            case 1: addItem(); break;
            case 2: viewItems(); break;
            case 3: searchItem(); break;
            case 4: updateItem(); break;
            case 5: deleteItem(); break;
            case 6: stockIn(); break;
            case 7: stockOut(); break;
            case 8: lowStockAlerts(); break;
            case 9: dashboard(); break;
            case 10: generateReport(); break;
            case 11: viewTransactionLog(); break;
            case 12:
                printf("Thank you for using Inventory Management System!\n");
                return 0;
        }
    }
    return 0;
}
