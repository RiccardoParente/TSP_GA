#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <fstream>
#include <math.h>
#include <numeric>
#include <algorithm>
#include <iterator>
#include "utimer.hpp"
#include <chrono>
#include <thread>
#include <barrier>
#include <mutex>
#include <condition_variable>
#include <vector>

using namespace std;

int tot_cities;
int population_size;
int iterations;
float **dist_matrix;
int nw;
mutex m;
condition_variable cv;
condition_variable cv2;
condition_variable cv3;
bool go = false;
bool go_copy = false;
bool go_fitness = false;
vector<thread> pool;
mutex *mutexes;

struct City
{
    int id;
    float x;
    float y;
};

struct Chromosome
{
    vector<int> path;
    float fitness;
    Chromosome(int n)
    {
        this->path = vector<int>(n);
    };
};

City *cities;
vector<Chromosome> population;
vector<Chromosome> temp_children;

void create_dist_matrix(char *file_path)
{
    int id;
    char buf[1024];
    float x, y;
    ifstream file(file_path);
    string line;
    bool start_data = false;
    while (getline(file, line))
    {
        if (line.find("EOF") != string::npos)
        {
            start_data = false;
        }
        else if (start_data)
        {
            int id = stoi(line.substr(0, line.find(" ")));
            cities[id - 1].id = id;
            line = line.erase(0, line.find(" ") + 1);
            cities[id - 1].x = stof(line.substr(0, line.find(" ")));
            line = line.erase(0, line.find(" ") + 1);
            cities[id - 1].y = stof(line);
        }
        else if ((line.find("DIMENSION")) != string::npos)
        {
            tot_cities = stoi(line.substr(line.find(": ") + 2));
            cities = (City *)malloc(sizeof(City) * tot_cities);
        }
        else if (line.find("NODE_COORD_SECTION") != string::npos)
        {
            start_data = true;
        }
    }
    dist_matrix = (float **)malloc(sizeof(float *) * tot_cities);
    for (int i = 0; i < tot_cities; i++)
        dist_matrix[i] = (float *)calloc(tot_cities, sizeof(float));
    for (int i = 0; i < tot_cities - 1; i++)
    {
        for (int j = i + 1; j < tot_cities; j++)
        {
            float distance = sqrt(pow(cities[i].x - cities[j].x, 2) + pow(cities[i].y - cities[j].y, 2));
            dist_matrix[i][j] = dist_matrix[j][i] = distance;
        }
    }
    free(cities);
}

void calculate_fitness(Chromosome *c)
{
    float distance = 0;
    for (int i = 0; i < tot_cities - 1; i++)
    {
        if (c->path[i] - 1 < 0 || c->path[i] - 1 >= tot_cities || c->path[i + 1] - 1 < 0 || c->path[i + 1] - 1 >= tot_cities)
        {
            for (int j = 0; j < tot_cities; j++)
            {
                cout << c->path[j] << ", ";
            }
        }
        distance += dist_matrix[c->path[i] - 1][c->path[i + 1] - 1];
    }
    distance += dist_matrix[c->path[tot_cities - 1] - 1][c->path[0] - 1];
    c->fitness = 1 / distance;
}

bool sort_by_fitness(const Chromosome &c1, const Chromosome &c2)
{
    return c1.fitness > c2.fitness;
}

void init_population(int start, int end)
{
    for (int i = start; i < end; i++)
    {
        iota(population[i].path.begin(), population[i].path.end(), 1);
        random_shuffle(population[i].path.begin(), population[i].path.end());
        calculate_fitness(&population[i]);
    }
    for (int i = start / 2; i < end / 2; i++)
    {
        fill(temp_children[i].path.begin(), temp_children[i].path.end(), 0);
    }
}

void sort_and_normalize()
{
    sort(population.begin(), population.end(), sort_by_fitness);
    float sum = 0;
    for (int i = 0; i < population_size; i++)
    {
        sum += population[i].fitness;
    }
    for (int i = 0; i < population_size; i++)
    {
        population[i].fitness /= sum;
    }
}

void mutate()
{
    for (int n = 0; n < population_size / 10; n++)
    {
        int best = population_size / 4;
        int i = rand() % tot_cities;
        int j = rand() % tot_cities;
        int k = rand() % (population_size - best);
        int temp = population[best + k].path[i];
        population[best + k].path[i] = population[best + k].path[j];
        population[best + k].path[j] = temp;
    }
}

void select_and_breed(int start, int end, barrier<void (*)()> &b, barrier<void (*)()> &b2, barrier<void (*)()> &b3)
{
    try
    {
        for (int iter = 0; iter < iterations; iter++)
        {
            {
                unique_lock lock(m);
                cv.wait(lock, []()
                        { return go; });
            }
            for (int i = start / 2; i < end / 2; i++)
            {
                float temp_fitness = 0;
                float r = ((float)rand()) / (RAND_MAX);
                for (int j = 0; j < population_size; j++)
                {
                    temp_fitness += population[j].fitness;
                    if (temp_fitness > r && i != j)
                    {
                        Chromosome *child = &temp_children[i];
                        int n = rand() % (tot_cities - 1);
                        int k = 0;
                        for (k = 0; k < n; k++)
                        {
                            child->path[k] = population[i].path[k];
                        }
                        int lseen = 0;
                        for (k = n; k < tot_cities; k++)
                        {
                            if (find(&child->path[0], &child->path[k], population[j].path[k]) == &child->path[k])
                            {
                                child->path[k] = population[j].path[k];
                            }
                            else
                            {
                                for (int l = lseen; l < k; l++)
                                {
                                    if (find(&child->path[0], &child->path[k], population[j].path[l]) == &child->path[k])
                                    {
                                        child->path[k] = population[j].path[l];
                                        lseen = l;
                                        break;
                                    }
                                }
                            }
                        }
                        calculate_fitness(child);
                        break;
                    }
                }
            }
            b.arrive_and_wait();
            {
                unique_lock lock(m);
                cv2.wait(lock, []()
                         { return go_copy; });
            }
            for (int i = start / 2; i < end / 2; i++)
            {
                population[(population_size / 2) + i] = temp_children[i];
            }
            b2.arrive_and_wait();
            {
                unique_lock lock(m);
                cv3.wait(lock, []()
                         { return go_fitness; });
            }
            for (int i = start / 2; i < end / 2; i++)
            {
                calculate_fitness(&population[(population_size / 2) + i]);
            }
            b3.arrive_and_wait();
        }
    }
    catch (const std::exception &e)
    {
        cout << e.what() << '\n';
    }
}

int main(int argc, char **argv)
{
    utimer t("ALL: ");
    if (argc < 5)
    {
        printf("Usage: ga_tsp_sequential <tsp_file_path> <populazion_size> <iterations> <nw>\n");
        exit(0);
    }
    population_size = stoi(argv[2]);
    iterations = stoi(argv[3]);
    nw = stoi(argv[4]) - 1;
    int max_nw = thread::hardware_concurrency() - 1;
    if (nw > max_nw)
    {
        nw = max_nw;
    }
    create_dist_matrix(argv[1]);
    for (int i = 0; i < population_size; i++)
    {
        population.push_back(Chromosome(tot_cities));
        if (i < population_size / 2)
        {
            temp_children.push_back(Chromosome(tot_cities));
        }
    }
    srand(time(NULL));
    int tsize = population_size / nw;
    int remainder = population_size % nw;
    int end = 0;
    int *divisions = (int *)calloc(nw + 1, sizeof(int));
    divisions[0] = 0;
    for (int i = 1; i < nw + 1; i++)
    {
        end += remainder > 0 ? tsize + 1 : tsize;
        divisions[i] = end;
        remainder--;
    }
    for (int i = 0; i < nw; i++)
    {
        pool.push_back(thread(init_population, divisions[i], divisions[i + 1]));
    }
    for (int i = 0; i < nw; i++)
    {
        pool[i].join();
    }
    pool.clear();
    sort_and_normalize();
    mutexes = (mutex *)malloc(population_size * sizeof(mutex));
    barrier<void (*)()> b(nw + 1, []()
                          { go = false; });
    barrier<void (*)()> b2(nw + 1, []()
                           { go_copy = false; });
    barrier<void (*)()> b3(nw + 1, []()
                           { go_fitness = false; });
    for (int i = 0; i < nw; i++)
    {
        pool.push_back(thread(select_and_breed, divisions[i], divisions[i + 1], ref(b), ref(b2), ref(b3)));
    }
    for (int iter = 0; iter < iterations; iter++)
    {
        {
            unique_lock lock(m);
            go = true;
            cv.notify_all();
        }
        b.arrive_and_wait();
        {
            unique_lock lock2(m);
            go_copy = true;
            cv2.notify_all();
        }
        b2.arrive_and_wait();
        mutate();
        {
            unique_lock lock3(m);
            go_fitness = true;
            cv3.notify_all();
        }
        b3.arrive_and_wait();
        sort_and_normalize();
    }
    for (int i = 0; i < nw; i++)
    {
        pool[i].join();
    }
    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < tot_cities; j++)
        {
            cout << population[i].path[j] << ", ";
        }
        cout << "- " << 1 / population[i].fitness << endl;
    }
}