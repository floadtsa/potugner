#include <stdlib.h> 
#include <string.h>
#include <pthread.h>
#include <curl/curl.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <stdatomic.h>

const char *user_agents[] = 
{
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/58.0.3029.110 Safari/537.36",
    "Mozilla/5.0 (Windows NT 6.1; WOW64; rv:41.0) Gecko/20100101 Firefox/41.0",
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_12_6) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/63.0.3239.132 Safari/537.36",
    "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:49.0) Gecko/20100101 Firefox/49.0",
    "Mozilla/5.0 (Linux; Android 8.1.0; Nexus 5 Build/OPM1.171019.019) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/63.0.3239.111 Mobile Safari/537.36",
    "Mozilla/5.0 (iPhone; CPU iPhone OS 11_2_5 like Mac OS X) AppleWebKit/537.36 (KHTML, like Gecko) Mobile/15D60 Safari/602.1"
};

size_t num_user_agents = sizeof(user_agents) / sizeof(user_agents[0]);

typedef struct //это типо для потока инфа
{
    const char *url;
    int id;
    time_t end_time;
    int think_ms;
    unsigned int rand_seed;
} thread_arg_t;

atomic_uint_least64_t total_requests = 0;
atomic_uint_least64_t success_requests = 0;
atomic_uint_least64_t failed_requests = 0;
pthread_mutex_t time_mutex = PTHREAD_MUTEX_INITIALIZER;
unsigned long long total_time_ns = 0;

static inline unsigned long long now_ns() //какая-то фигня для времени
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void *worker(void *arg) //тут основк=ная часть(робота curl)
{
    thread_arg_t *ta = (thread_arg_t *)arg;
    CURL *curl = curl_easy_init();
    if (!curl) 
    {
        fprintf(stderr, "Thread %d: curl_easy_init failed\n", ta->id);
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, ta->url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    const char *user_agent = user_agents[rand_r(&ta->rand_seed) % num_user_agents];//кароче тут рандомно генерируется user agent
    curl_easy_setopt(curl, CURLOPT_USERAGENT, user_agent);

    while (time(NULL) < ta->end_time) {
        unsigned long long t1 = now_ns();
        CURLcode res = curl_easy_perform(curl);
        unsigned long long t2 = now_ns();
        atomic_fetch_add(&total_requests, 1);

        if (res == CURLE_OK) 
        {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code >= 200 && http_code < 300) 
            {
                atomic_fetch_add(&success_requests, 1);
            } else 
            {
                atomic_fetch_add(&failed_requests, 1);
            }
        } 
        else 
        {
            atomic_fetch_add(&failed_requests, 1);
        }

        unsigned long long elapsed = (t2 - t1);
        pthread_mutex_lock(&time_mutex);
        total_time_ns += elapsed;
        pthread_mutex_unlock(&time_mutex);

        if (ta->think_ms > 0) {
            usleep((useconds_t)ta->think_ms * 1000);
        }
    }

    curl_easy_cleanup(curl);
    return NULL;
}

int main(int argc, char **argv) 
{
    if (argc < 5) 
    {
        fprintf(stderr, "Usage: %s <url> <threads> <duration_sec> <think_ms>\n", argv[0]);
        return 1;
    }

    const char *url = argv[1];
    int threads = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int think_ms = atoi(argv[4]);

    if (threads <= 0 || duration <= 0) 
    {
        fprintf(stderr, "threads and duration must be > 0\n");
        return 1;
    }

    curl_global_init(CURL_GLOBAL_ALL);
    srand(time(NULL));

    pthread_t *tids = malloc(sizeof(pthread_t) * threads);
    thread_arg_t *args = malloc(sizeof(thread_arg_t) * threads);
    time_t end_time = time(NULL) + duration;

    for (int i = 0; i < threads; ++i) 
    {
        args[i].url = url;
        args[i].id = i;
        args[i].end_time = end_time;
        args[i].think_ms = think_ms;
        args[i].rand_seed = rand();

        if (pthread_create(&tids[i], NULL, worker, &args[i]) != 0) 
        {
            perror("pthread_create");
            return 1;
        }
    }

    for (int i = 0; i < threads; ++i) 
    {
        pthread_join(tids[i], NULL);
    }

    unsigned long long tot = atomic_load(&total_requests);
    unsigned long long succ = atomic_load(&success_requests);
    unsigned long long fail = atomic_load(&failed_requests);
    unsigned long long sum_ns;
    pthread_mutex_lock(&time_mutex);
    sum_ns = total_time_ns;
    pthread_mutex_unlock(&time_mutex);

    double avg_ms = tot ? (double)sum_ns / (double)tot / 1e6 : 0.0;
    double rps = (double)tot / (double)duration;

    printf("\n--- Results ---\n");
    printf("URL: %s\n", url);
    printf("Threads: %d\n", threads);
    printf("Duration (s): %d\n", duration);
    printf("Total requests: %llu\n", tot);
    printf("Successful (2xx): %llu\n", succ);
    printf("Failed: %llu\n", fail);
    printf("Avg latency: %.2f ms\n", avg_ms);
    printf("Requests/sec (approx): %.2f\n", rps);

    free(tids);
    free(args);
    curl_global_cleanup();
    return 0;
}
