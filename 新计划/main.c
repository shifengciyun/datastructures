/*=============================================
 *  文件: main.c
 *  说明: 自习室预约管理系统 - 单文件实现
 *  版本: v2.0.0
 *=============================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*===== 1. 常量与数据类型定义 =====*/

#define MAX_NAME_LEN 50
#define MAX_PASS_LEN 50
#define MAX_LINE_LEN 256
#define HASH_SIZE 101
#define MAX_SEATS 100
#define INITIAL_CREDIT 100
#define CREDIT_PENALTY_LATE 5
#define CREDIT_PENALTY_CANCEL 2
#define CREDIT_REWARD_NORMAL 1
#define CANCEL_THRESHOLD_MIN 30
#define CHECKIN_TIMEOUT_MIN 15
#define ADMIN_PASSWORD "admin123"

typedef enum {
    SUCCESS = 0,
    ERROR_NULL_PTR,
    ERROR_NOT_FOUND,
    ERROR_ALREADY_EXISTS,
    ERROR_INVALID_PARAM,
    ERROR_MEMORY,
    ERROR_FILE,
    ERROR_FULL,
    ERROR_CREDIT_LOW,
    ERROR_TIME_CONFLICT,
    ERROR_STATUS
} ErrorCode;

typedef enum {
    SEAT_AVAILABLE = 0,
    SEAT_RESERVED,
    SEAT_IN_USE
} SeatStatus;

typedef enum {
    RES_PENDING = 0,
    RES_ACTIVE,
    RES_COMPLETED,
    RES_CANCELLED,
    RES_TIMEOUT
} ResStatus;

typedef struct Student {
    int id;
    char name[MAX_NAME_LEN];
    char password[MAX_PASS_LEN];
    int creditScore;
} Student;

typedef struct Seat {
    int id;
    int roomId;
    SeatStatus status;
    int studentId;
} Seat;

typedef struct Reservation {
    int id;
    int studentId;
    int seatId;
    time_t startTime;
    time_t endTime;
    ResStatus status;
} Reservation;

typedef struct Node {
    void *data;
    struct Node *next;
} Node;

typedef struct {
    Node *buckets[HASH_SIZE];
    int count;
} HashTable;

typedef struct {
    Node *front;
    Node *rear;
    int count;
} Queue;

/*===== 2. 全局变量 =====*/

static HashTable g_students;
static Node *g_seats = NULL;
static Node *g_reservations = NULL;
static int g_seatCount = 0;
static int g_nextStudentId = 1;
static int g_nextResId = 1;
static int g_currentUserId = -1;
static const char *DATA_FILE = "data.txt";

/*===== 3. 工具函数 =====*/

void *SafeMalloc(size_t size)
{
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "错误: 内存分配失败\n");
        exit(EXIT_FAILURE);
    }
    memset(ptr, 0, size);
    return ptr;
}

char *SafeStrDup(const char *src)
{
    if (!src) return NULL;
    size_t len = strlen(src) + 1;
    char *dst = SafeMalloc(len);
    strncpy(dst, src, len - 1);
    dst[len - 1] = '\0';
    return dst;
}

unsigned int BKDRHash(const char *str)
{
    unsigned int seed = 131;
    unsigned int hash = 0;
    while (*str) {
        hash = hash * seed + (unsigned char)(*str++);
    }
    return hash % HASH_SIZE;
}

void FormatTime(time_t t, char *buf, size_t bufSize)
{
    struct tm *tm_info = localtime(&t);
    strftime(buf, bufSize, "%Y-%m-%d %H:%M", tm_info);
}

const char *ResStatusStr(ResStatus s)
{
    switch (s) {
    case RES_PENDING:   return "待签到";
    case RES_ACTIVE:    return "使用中";
    case RES_COMPLETED: return "已完成";
    case RES_CANCELLED: return "已取消";
    case RES_TIMEOUT:   return "超时取消";
    default:            return "未知";
    }
}

/*===== 4. 链表实现 =====*/

Node *ListCreateNode(void *data)
{
    Node *node = SafeMalloc(sizeof(Node));
    node->data = data;
    node->next = NULL;
    return node;
}

void ListPushFront(Node **head, void *data)
{
    Node *node = ListCreateNode(data);
    node->next = *head;
    *head = node;
}

Node *ListFind(Node *head, const void *key,
               int (*cmp)(const void *, const void *))
{
    Node *cur = head;
    while (cur) {
        if (cmp(cur->data, key) == 0) return cur;
        cur = cur->next;
    }
    return NULL;
}

ErrorCode ListRemove(Node **head, const void *key,
                     int (*cmp)(const void *, const void *),
                     void (*freeData)(void *))
{
    Node *prev = NULL, *cur = *head;
    while (cur) {
        if (cmp(cur->data, key) == 0) {
            if (prev) prev->next = cur->next;
            else *head = cur->next;
            if (freeData) freeData(cur->data);
            free(cur);
            return SUCCESS;
        }
        prev = cur;
        cur = cur->next;
    }
    return ERROR_NOT_FOUND;
}

void ListDestroy(Node **head, void (*freeData)(void *))
{
    Node *cur = *head;
    while (cur) {
        Node *next = cur->next;
        if (freeData) freeData(cur->data);
        free(cur);
        cur = next;
    }
    *head = NULL;
}

int ListCount(Node *head)
{
    int count = 0;
    while (head) { count++; head = head->next; }
    return count;
}

/*===== 5. 哈希表实现 =====*/

void HashInit(HashTable *ht)
{
    memset(ht->buckets, 0, sizeof(ht->buckets));
    ht->count = 0;
}

ErrorCode HashInsert(HashTable *ht, const char *key, void *data)
{
    unsigned int idx = BKDRHash(key);
    Node *node = ListCreateNode(data);
    node->next = ht->buckets[idx];
    ht->buckets[idx] = node;
    ht->count++;
    return SUCCESS;
}

void *HashFind(HashTable *ht, const char *key)
{
    unsigned int idx = BKDRHash(key);
    Node *cur = ht->buckets[idx];
    while (cur) {
        Student *s = (Student *)cur->data;
        if (strcmp(s->name, key) == 0) return cur->data;
        cur = cur->next;
    }
    return NULL;
}

void HashDestroy(HashTable *ht, void (*freeData)(void *))
{
    for (int i = 0; i < HASH_SIZE; i++) {
        ListDestroy(&ht->buckets[i], freeData);
    }
    ht->count = 0;
}

/*===== 6. 队列实现 =====*/

void QueueInit(Queue *q)
{
    q->front = q->rear = NULL;
    q->count = 0;
}

void QueuePush(Queue *q, void *data)
{
    Node *node = ListCreateNode(data);
    if (q->rear) q->rear->next = node;
    else q->front = node;
    q->rear = node;
    q->count++;
}

void *QueuePop(Queue *q)
{
    if (!q->front) return NULL;
    Node *node = q->front;
    void *data = node->data;
    q->front = node->next;
    if (!q->front) q->rear = NULL;
    free(node);
    q->count--;
    return data;
}

/*===== 7. 学生管理 =====*/

int StudentCmp(const void *a, const void *b)
{
    const Student *sa = (const Student *)a;
    const Student *sb = (const Student *)b;
    return sa->id - sb->id;
}

int StudentNameCmp(const void *a, const void *b)
{
    const Student *s = (const Student *)a;
    const char *name = (const char *)b;
    return strcmp(s->name, name);
}

ErrorCode StudentRegister(const char *name, const char *password)
{
    if (!name || !password) return ERROR_NULL_PTR;
    if (strlen(name) == 0 || strlen(password) == 0)
        return ERROR_INVALID_PARAM;

    if (HashFind(&g_students, name))
        return ERROR_ALREADY_EXISTS;

    Student *s = SafeMalloc(sizeof(Student));
    s->id = g_nextStudentId++;
    strncpy(s->name, name, MAX_NAME_LEN - 1);
    strncpy(s->password, password, MAX_PASS_LEN - 1);
    s->creditScore = INITIAL_CREDIT;

    HashInsert(&g_students, name, s);
    printf("注册成功！学号: %d\n", s->id);
    return SUCCESS;
}

ErrorCode StudentLogin(const char *name, const char *password)
{
    if (!name || !password) return ERROR_NULL_PTR;

    Student *s = HashFind(&g_students, name);
    if (!s) return ERROR_NOT_FOUND;
    if (strcmp(s->password, password) != 0)
        return ERROR_NOT_FOUND;  // 密码错误也返回未找到，不泄露信息

    g_currentUserId = s->id;
    printf("登录成功！欢迎 %s (信用分: %d)\n", s->name, s->creditScore);
    return SUCCESS;
}

void StudentLogout(void)
{
    g_currentUserId = -1;
    printf("已退出登录\n");
}

Student *FindStudentById(int id)
{
    for (int i = 0; i < HASH_SIZE; i++) {
        Node *cur = g_students.buckets[i];
        while (cur) {
            Student *s = (Student *)cur->data;
            if (s->id == id) return s;
            cur = cur->next;
        }
    }
    return NULL;
}

ErrorCode UpdateCredit(int studentId, int delta)
{
    Student *s = FindStudentById(studentId);
    if (!s) return ERROR_NOT_FOUND;
    s->creditScore += delta;
    if (s->creditScore < 0) s->creditScore = 0;
    return SUCCESS;
}

/*===== 8. 座位管理 =====*/

ErrorCode InitSeats(int roomId, int count)
{
    if (count <= 0 || count > MAX_SEATS) return ERROR_INVALID_PARAM;

    ListDestroy(&g_seats, free);
    ListDestroy(&g_reservations, free);
    g_seatCount = 0;
    g_nextResId = 1;

    for (int i = 1; i <= count; i++) {
        Seat *seat = SafeMalloc(sizeof(Seat));
        seat->id = i;
        seat->roomId = roomId;
        seat->status = SEAT_AVAILABLE;
        seat->studentId = 0;
        ListPushFront(&g_seats, seat);
        g_seatCount++;
    }
    printf("已初始化 %d 个座位 (自习室 %d)\n", count, roomId);
    return SUCCESS;
}

Seat *FindSeatById(int seatId)
{
    Node *cur = g_seats;
    while (cur) {
        Seat *seat = (Seat *)cur->data;
        if (seat->id == seatId) return seat;
        cur = cur->next;
    }
    return NULL;
}

void ShowAvailableSeats(void)
{
    printf("\n=== 可用座位 ===\n");
    printf("%-6s %-8s %-10s\n", "座位号", "自习室", "状态");
    printf("------------------------\n");

    int available = 0;
    Node *cur = g_seats;
    while (cur) {
        Seat *seat = (Seat *)cur->data;
        const char *status = (seat->status == SEAT_AVAILABLE) ? "空闲" :
                             (seat->status == SEAT_RESERVED) ? "已预约" : "使用中";
        printf("%-6d %-8d %-10s\n", seat->id, seat->roomId, status);
        if (seat->status == SEAT_AVAILABLE) available++;
        cur = cur->next;
    }
    printf("------------------------\n");
    printf("空闲座位: %d/%d\n", available, g_seatCount);
}

void ShowAllSeats(void)
{
    printf("\n=== 所有座位状态 ===\n");
    printf("%-6s %-8s %-10s %-8s\n", "座位号", "自习室", "状态", "使用者");
    printf("-------------------------------\n");

    Node *cur = g_seats;
    while (cur) {
        Seat *seat = (Seat *)cur->data;
        const char *status = (seat->status == SEAT_AVAILABLE) ? "空闲" :
                             (seat->status == SEAT_RESERVED) ? "已预约" : "使用中";
        printf("%-6d %-8d %-10s %-8d\n",
               seat->id, seat->roomId, status, seat->studentId);
        cur = cur->next;
    }
}

/*===== 9. 预约管理 =====*/

int IsTimeOverlap(time_t s1, time_t e1, time_t s2, time_t e2)
{
    return s1 < e2 && s2 < e1;
}

int ResSeatCmp(const void *a, const void *b)
{
    const Reservation *r = (const Reservation *)a;
    int seatId = *(const int *)b;
    return r->seatId - seatId;
}

ErrorCode CheckConflict(int seatId, time_t start, time_t end)
{
    Node *cur = g_reservations;
    while (cur) {
        Reservation *r = (Reservation *)cur->data;
        if (r->seatId == seatId &&
            (r->status == RES_PENDING || r->status == RES_ACTIVE)) {
            if (IsTimeOverlap(start, end, r->startTime, r->endTime)) {
                return ERROR_TIME_CONFLICT;
            }
        }
        cur = cur->next;
    }
    return SUCCESS;
}

ErrorCode CreateReservation(int seatId, time_t start, time_t end)
{
    if (g_currentUserId < 0) return ERROR_STATUS;

    Student *stu = FindStudentById(g_currentUserId);
    if (!stu) return ERROR_NOT_FOUND;
    if (stu->creditScore < 10) return ERROR_CREDIT_LOW;

    Seat *seat = FindSeatById(seatId);
    if (!seat) return ERROR_NOT_FOUND;
    if (seat->status != SEAT_AVAILABLE) return ERROR_STATUS;

    if (difftime(end, start) <= 0) return ERROR_INVALID_PARAM;

    ErrorCode err = CheckConflict(seatId, start, end);
    if (err != SUCCESS) return err;

    Reservation *r = SafeMalloc(sizeof(Reservation));
    r->id = g_nextResId++;
    r->studentId = g_currentUserId;
    r->seatId = seatId;
    r->startTime = start;
    r->endTime = end;
    r->status = RES_PENDING;

    ListPushFront(&g_reservations, r);
    seat->status = SEAT_RESERVED;
    seat->studentId = g_currentUserId;

    char startBuf[64], endBuf[64];
    FormatTime(start, startBuf, sizeof(startBuf));
    FormatTime(end, endBuf, sizeof(endBuf));
    printf("预约成功！预约号: %d\n", r->id);
    printf("座位: %d | 时间: %s ~ %s\n", seatId, startBuf, endBuf);
    return SUCCESS;
}

Reservation *FindReservationById(int resId)
{
    Node *cur = g_reservations;
    while (cur) {
        Reservation *r = (Reservation *)cur->data;
        if (r->id == resId) return r;
        cur = cur->next;
    }
    return NULL;
}

ErrorCode CancelReservation(int resId)
{
    Reservation *r = FindReservationById(resId);
    if (!r) return ERROR_NOT_FOUND;
    if (r->studentId != g_currentUserId) return ERROR_STATUS;
    if (r->status != RES_PENDING) return ERROR_STATUS;

    time_t now = time(NULL);
    double diff = difftime(r->startTime, now) / 60.0;

    if (diff < CANCEL_THRESHOLD_MIN) {
        UpdateCredit(g_currentUserId, -CREDIT_PENALTY_CANCEL);
        printf("取消不足%d分钟，扣除%d信用分\n",
               CANCEL_THRESHOLD_MIN, CREDIT_PENALTY_CANCEL);
    }

    r->status = RES_CANCELLED;
    Seat *seat = FindSeatById(r->seatId);
    if (seat) {
        seat->status = SEAT_AVAILABLE;
        seat->studentId = 0;
    }

    printf("预约 %d 已取消\n", resId);
    return SUCCESS;
}

ErrorCode CheckIn(int resId)
{
    Reservation *r = FindReservationById(resId);
    if (!r) return ERROR_NOT_FOUND;
    if (r->studentId != g_currentUserId) return ERROR_STATUS;
    if (r->status != RES_PENDING) return ERROR_STATUS;

    time_t now = time(NULL);

    // 检查预约是否已过期
    if (now > r->endTime) {
        r->status = RES_TIMEOUT;
        UpdateCredit(g_currentUserId, -CREDIT_PENALTY_LATE);
        Seat *seat = FindSeatById(r->seatId);
        if (seat) {
            seat->status = SEAT_AVAILABLE;
            seat->studentId = 0;
        }
        printf("预约已过期，无法签到，扣除%d信用分\n", CREDIT_PENALTY_LATE);
        return ERROR_STATUS;
    }

    double diff = difftime(now, r->startTime) / 60.0;

    if (diff > CHECKIN_TIMEOUT_MIN) {
        r->status = RES_TIMEOUT;
        UpdateCredit(g_currentUserId, -CREDIT_PENALTY_LATE);
        Seat *seat = FindSeatById(r->seatId);
        if (seat) {
            seat->status = SEAT_AVAILABLE;
            seat->studentId = 0;
        }
        printf("签到超时，预约已取消，扣除%d信用分\n", CREDIT_PENALTY_LATE);
        return ERROR_STATUS;
    }

    r->status = RES_ACTIVE;
    Seat *seat = FindSeatById(r->seatId);
    if (seat) seat->status = SEAT_IN_USE;

    printf("签到成功！座位 %d\n", r->seatId);
    return SUCCESS;
}

ErrorCode CheckOut(int resId)
{
    Reservation *r = FindReservationById(resId);
    if (!r) return ERROR_NOT_FOUND;
    if (r->studentId != g_currentUserId) return ERROR_STATUS;
    if (r->status != RES_ACTIVE) return ERROR_STATUS;

    r->status = RES_COMPLETED;
    UpdateCredit(g_currentUserId, CREDIT_REWARD_NORMAL);

    Seat *seat = FindSeatById(r->seatId);
    if (seat) {
        seat->status = SEAT_AVAILABLE;
        seat->studentId = 0;
    }

    printf("签退成功！信用分+%d\n", CREDIT_REWARD_NORMAL);
    return SUCCESS;
}

void ShowMyReservations(void)
{
    printf("\n=== 我的预约记录 ===\n");
    printf("%-6s %-6s %-20s %-20s %-10s\n",
           "预约号", "座位", "开始时间", "结束时间", "状态");
    printf("------------------------------------------------------------\n");

    int count = 0;
    Node *cur = g_reservations;
    while (cur) {
        Reservation *r = (Reservation *)cur->data;
        if (r->studentId == g_currentUserId) {
            char startBuf[64], endBuf[64];
            FormatTime(r->startTime, startBuf, sizeof(startBuf));
            FormatTime(r->endTime, endBuf, sizeof(endBuf));
            printf("%-6d %-6d %-20s %-20s %-10s\n",
                   r->id, r->seatId,
                   startBuf, endBuf,
                   ResStatusStr(r->status));
            count++;
        }
        cur = cur->next;
    }

    if (count == 0) printf("暂无预约记录\n");
}

/*===== 10. 超时管理 =====*/

void CheckTimeoutReservations(void)
{
    time_t now = time(NULL);
    Node *cur = g_reservations;
    while (cur) {
        Reservation *r = (Reservation *)cur->data;
        if (r->status == RES_PENDING) {
            double diff = difftime(now, r->startTime) / 60.0;
            if (diff > CHECKIN_TIMEOUT_MIN) {
                r->status = RES_TIMEOUT;
                UpdateCredit(r->studentId, -CREDIT_PENALTY_LATE);
                Seat *seat = FindSeatById(r->seatId);
                if (seat) {
                    seat->status = SEAT_AVAILABLE;
                    seat->studentId = 0;
                }
                printf("预约 %d 超时自动取消 (学生: %d)\n",
                       r->id, r->studentId);
            }
        }
        cur = cur->next;
    }
}

/*===== 11. 数据持久化 =====*/

ErrorCode SaveData(void)
{
    FILE *fp = fopen(DATA_FILE, "w");
    if (!fp) return ERROR_FILE;

    fprintf(fp, "[META]\n");
    fprintf(fp, "nextStudentId=%d\n", g_nextStudentId);
    fprintf(fp, "nextResId=%d\n", g_nextResId);
    fprintf(fp, "seatCount=%d\n", g_seatCount);

    fprintf(fp, "\n[STUDENTS]\n");
    for (int i = 0; i < HASH_SIZE; i++) {
        Node *cur = g_students.buckets[i];
        while (cur) {
            Student *s = (Student *)cur->data;
            fprintf(fp, "%d,%s,%s,%d\n",
                    s->id, s->name, s->password, s->creditScore);
            cur = cur->next;
        }
    }

    fprintf(fp, "\n[SEATS]\n");
    Node *cur = g_seats;
    while (cur) {
        Seat *seat = (Seat *)cur->data;
        fprintf(fp, "%d,%d,%d,%d\n",
                seat->id, seat->roomId, seat->status, seat->studentId);
        cur = cur->next;
    }

    fprintf(fp, "\n[RESERVATIONS]\n");
    cur = g_reservations;
    while (cur) {
        Reservation *r = (Reservation *)cur->data;
        fprintf(fp, "%d,%d,%d,%ld,%ld,%d\n",
                r->id, r->studentId, r->seatId,
                (long)r->startTime, (long)r->endTime, r->status);
        cur = cur->next;
    }

    fclose(fp);
    return SUCCESS;
}

ErrorCode LoadData(void)
{
    FILE *fp = fopen(DATA_FILE, "r");
    if (!fp) return ERROR_FILE;

    char line[MAX_LINE_LEN];
    enum { META, STUDENTS, SEATS, RESERVATIONS } section = META;

    while (fgets(line, sizeof(line), fp)) {
        // 去除换行符
        line[strcspn(line, "\r\n")] = '\0';
        if (strlen(line) == 0) continue;

        if (strcmp(line, "[META]") == 0) { section = META; continue; }
        if (strcmp(line, "[STUDENTS]") == 0) { section = STUDENTS; continue; }
        if (strcmp(line, "[SEATS]") == 0) { section = SEATS; continue; }
        if (strcmp(line, "[RESERVATIONS]") == 0) { section = RESERVATIONS; continue; }

        switch (section) {
        case META:
            if (sscanf(line, "nextStudentId=%d", &g_nextStudentId) == 1) break;
            if (sscanf(line, "nextResId=%d", &g_nextResId) == 1) break;
            if (sscanf(line, "seatCount=%d", &g_seatCount) == 1) break;
            break;

        case STUDENTS: {
            Student *s = SafeMalloc(sizeof(Student));
            if (sscanf(line, "%d,%49[^,],%49[^,],%d",
                       &s->id, s->name, s->password, &s->creditScore) == 4) {
                HashInsert(&g_students, s->name, s);
            } else {
                free(s);
            }
            break;
        }

        case SEATS: {
            Seat *seat = SafeMalloc(sizeof(Seat));
            int status;
            if (sscanf(line, "%d,%d,%d,%d",
                       &seat->id, &seat->roomId, &status, &seat->studentId) == 4) {
                seat->status = (SeatStatus)status;
                ListPushFront(&g_seats, seat);
            } else {
                free(seat);
            }
            break;
        }

        case RESERVATIONS: {
            Reservation *r = SafeMalloc(sizeof(Reservation));
            int status;
            long start, end;
            if (sscanf(line, "%d,%d,%d,%ld,%ld,%d",
                       &r->id, &r->studentId, &r->seatId,
                       &start, &end, &status) == 6) {
                r->startTime = (time_t)start;
                r->endTime = (time_t)end;
                r->status = (ResStatus)status;
                ListPushFront(&g_reservations, r);
            } else {
                free(r);
            }
            break;
        }
        }
    }

    fclose(fp);
    printf("数据加载成功\n");
    return SUCCESS;
}

/*===== 12. 主程序 =====*/

void ShowStudentMenu(void)
{
    printf("\n===== 学生菜单 =====\n");
    printf("1. 查看可用座位\n");
    printf("2. 预约座位\n");
    printf("3. 取消预约\n");
    printf("4. 签到\n");
    printf("5. 签退\n");
    printf("6. 我的预约\n");
    printf("7. 退出登录\n");
    printf("请选择: ");
}

void ShowAdminMenu(void)
{
    printf("\n===== 管理员菜单 =====\n");
    printf("1. 初始化自习室\n");
    printf("2. 查看所有座位\n");
    printf("3. 重置系统\n");
    printf("4. 返回\n");
    printf("请选择: ");
}

void ShowMainMenu(void)
{
    printf("\n===== 自习室预约系统 =====\n");
    printf("1. 注册\n");
    printf("2. 登录\n");
    printf("3. 管理员\n");
    printf("4. 保存数据\n");
    printf("5. 退出\n");
    printf("请选择: ");
}

void HandleStudent(void)
{
    int choice;
    while (1) {
        CheckTimeoutReservations();
        ShowStudentMenu();
        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n');
            printf("输入无效，请重新选择\n");
            continue;
        }

        switch (choice) {
        case 1:
            ShowAvailableSeats();
            break;
        case 2: {
            int seatId;
            int startH, startM, endH, endM;
            printf("输入座位号: ");
            scanf("%d", &seatId);
            printf("输入开始时间 (小时 分钟): ");
            scanf("%d %d", &startH, &startM);
            printf("输入结束时间 (小时 分钟): ");
            scanf("%d %d", &endH, &endM);

            if (startH < 0 || startH > 23 || startM < 0 || startM > 59 ||
                endH < 0 || endH > 23 || endM < 0 || endM > 59) {
                printf("时间格式错误，小时范围0-23，分钟范围0-59\n");
                break;
            }

            time_t now = time(NULL);
            struct tm *tm = localtime(&now);
            tm->tm_hour = startH; tm->tm_min = startM; tm->tm_sec = 0;
            time_t start = mktime(tm);
            tm->tm_hour = endH; tm->tm_min = endM;
            time_t end = mktime(tm);

            if (end <= start) {
                printf("结束时间必须晚于开始时间\n");
            } else {
                CreateReservation(seatId, start, end);
            }
            break;
        }
        case 3: {
            int resId;
            printf("输入预约号: ");
            scanf("%d", &resId);
            CancelReservation(resId);
            break;
        }
        case 4: {
            int resId;
            printf("输入预约号: ");
            scanf("%d", &resId);
            CheckIn(resId);
            break;
        }
        case 5: {
            int resId;
            printf("输入预约号: ");
            scanf("%d", &resId);
            CheckOut(resId);
            break;
        }
        case 6:
            ShowMyReservations();
            break;
        case 7:
            StudentLogout();
            return;
        default:
            printf("无效选择\n");
        }
    }
}

void HandleAdmin(void)
{
    int choice;
    while (1) {
        ShowAdminMenu();
        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n');
            printf("输入无效，请重新选择\n");
            continue;
        }

        switch (choice) {
        case 1: {
            int roomId, count;
            printf("输入自习室号: ");
            scanf("%d", &roomId);
            printf("输入座位数量: ");
            scanf("%d", &count);
            InitSeats(roomId, count);
            break;
        }
        case 2:
            ShowAllSeats();
            break;
        case 3:
            ListDestroy(&g_seats, free);
            ListDestroy(&g_reservations, free);
            HashDestroy(&g_students, free);
            g_seatCount = 0;
            g_nextStudentId = 1;
            g_nextResId = 1;
            printf("系统已重置\n");
            break;
        case 4:
            return;
        default:
            printf("无效选择\n");
        }
    }
}

int main(void)
{
    system("chcp 65001 >nul");  // 设置控制台为UTF-8编码，解决中文乱码
    printf("正在加载数据...\n");
    if (LoadData() != SUCCESS) {
        printf("未找到数据文件，首次运行请先进入管理员菜单初始化自习室\n");
    }

    int choice;
    while (1) {
        ShowMainMenu();
        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n');
            printf("输入无效，请重新选择\n");
            continue;
        }

        switch (choice) {
        case 1: {
            char name[MAX_NAME_LEN], pass[MAX_PASS_LEN];
            printf("输入用户名: ");
            scanf("%49s", name);
            printf("输入密码: ");
            scanf("%49s", pass);
            StudentRegister(name, pass);
            break;
        }
        case 2: {
            char name[MAX_NAME_LEN], pass[MAX_PASS_LEN];
            printf("输入用户名: ");
            scanf("%49s", name);
            printf("输入密码: ");
            scanf("%49s", pass);
            if (StudentLogin(name, pass) == SUCCESS) {
                HandleStudent();
            }
            break;
        }
        case 3: {
            char pass[MAX_PASS_LEN];
            printf("请输入管理员密码: ");
            scanf("%49s", pass);
            if (strcmp(pass, ADMIN_PASSWORD) == 0) {
                HandleAdmin();
            } else {
                printf("密码错误\n");
            }
            break;
        }
        case 4:
            if (SaveData() == SUCCESS)
                printf("数据保存成功\n");
            else
                printf("数据保存失败\n");
            break;
        case 5:
            SaveData();
            ListDestroy(&g_seats, free);
            ListDestroy(&g_reservations, free);
            HashDestroy(&g_students, free);
            printf("感谢使用，再见！\n");
            return 0;
        default:
            printf("无效选择\n");
        }
    }

    return 0;
}
