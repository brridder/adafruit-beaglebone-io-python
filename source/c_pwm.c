#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "c_pwm.h"

#define KEYLEN 7

#define PERIOD 0
#define DUTY 1

char pwm_ctrl_dir[30];
char ocp_dir[22];
int pwm_initialized = 0;

// pwm exports
struct pwm_exp
{
    char key[KEYLEN];
    int period_fd;
    int duty_fd;
    unsigned long duty;
    unsigned long period_ns;
    struct pwm_exp *next;
};
struct pwm_exp *exported_pwms = NULL;

int load_device_tree(const char *name);

int fd_lookup(const char *key, int type_fd)
{
    struct pwm_exp *pwm = exported_pwms;
    while (pwm != NULL)
    {
        if (strcmp(pwm->key, key)) {
            if (type_fd == DUTY) {
                return pwm->duty_fd;
            } else {
                return pwm->period_fd;
            }
        }
        pwm = pwm->next;
    }
    return 0;
}

struct pwm_exp *lookup_exported_pwm(const char *key) 
{
    struct pwm_exp *pwm = exported_pwms;

    while (pwm != NULL)
    {
        if (strcmp(pwm->key, key)) {
            return pwm;
        }
        pwm = pwm->next;
    }
    return 0;
}

int build_path(const char *partial_path, const char *prefix, char *full_path, size_t full_path_len)
{
    DIR *dp;
    struct dirent *ep;

    dp = opendir (partial_path);
    if (dp != NULL) {
        while ((ep = readdir (dp))) {
            if (strstr(ep->d_name, prefix)) {
                snprintf(full_path, full_path_len, "%s%s", partial_path, ep->d_name);
                (void) closedir (dp);
                return 1;
            }
        }
        (void) closedir (dp);
    } else {
        return 0;
    }

    return 0;
}

int initialize_pwm(void)
{
    if  (build_path("/sys/devices/", "bone_capemgr", pwm_ctrl_dir, sizeof(pwm_ctrl_dir)) && 
         build_path("/sys/devices/", "ocp", ocp_dir, sizeof(ocp_dir))) {
        pwm_initialized = 1;

        load_device_tree("am33xx_pwm");
        return 1;
    }
    return 0;   
}

int load_device_tree(const char *name)
{
    FILE *file = NULL;
    char slots[40];
    char line[256];

    if(!pwm_initialized) {
        initialize_pwm();
    }

    snprintf(slots, sizeof(slots), "%s/slots", pwm_ctrl_dir);

    file = fopen(slots, "r+");
    if (!file) {
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        //the device is already loaded, return 1
        if (strstr(line, name)) {
            fclose(file);
            return 1;
        }
    }

    //if the device isn't already loaded, load it, and return
    fprintf(file, name);
    fclose(file);

    return 1;
}

int unload_device_tree(const char *name)
{
    FILE *file = NULL;
    char slots[40];
    char line[256];
    char *slot_line;

    snprintf(slots, sizeof(slots), "%s/slots", pwm_ctrl_dir);

    file = fopen(slots, "r+");
    if (!file) {
        return -1;
    }

    while (fgets(line, sizeof(line), file)) {
        //the device is loaded, let's unload it
        if (strstr(line, name)) {
            slot_line = strtok(line, ":");
            //remove leading spaces
            while(*slot_line == ' ')
                slot_line++;

            fprintf(file, "-%s", slot_line);
            fclose(file);
            return 1;
        }
    }

    //not loaded, close file
    fclose(file);

    return 1;
}

int pwm_enable(const char *key)
{
    char fragment[18];
    char pwm_test_fragment[20];
    char pwm_test_path[45];
    char period_path[50];
    char duty_path[50];
    int period_fd, duty_fd;
    struct pwm_exp *new_pwm, *pwm;

    snprintf(fragment, sizeof(fragment), "bone_pwm_%s", key);
    

    if (!load_device_tree(fragment)) {
        //error enabling pin for pwm
        return -1;
    }

    //creates the fragment in order to build the pwm_test_filename, such as "pwm_test_P9_13"
    snprintf(pwm_test_fragment, sizeof(pwm_test_fragment), "pwm_test_%s", key);

    //finds and builds the pwm_test_path, as it can be variable...
    build_path(ocp_dir, pwm_test_fragment, pwm_test_path, sizeof(pwm_test_path));

    //create the path for the period and duty
    snprintf(period_path, sizeof(period_path), "%s/period", pwm_test_path);
    snprintf(duty_path, sizeof(duty_path), "%s/duty", pwm_test_path);


    //TODO add fd to list
    
    if ((period_fd = open(period_path, O_RDWR)) < 0)
        return -1;

    if ((duty_fd = open(duty_path, O_RDWR)) < 0) {
        //error, close already opened period_fd.
        close(period_fd);
        return -1;
    }

    // add to list
    new_pwm = malloc(sizeof(struct pwm_exp));
    if (new_pwm == 0) {
        return -1; // out of memory
    }

    strncpy(new_pwm->key, key, KEYLEN);
    new_pwm->period_fd = period_fd;
    new_pwm->duty_fd = duty_fd;
    new_pwm->next = NULL;

    if (exported_pwms == NULL)
    {
        // create new list
        exported_pwms = new_pwm;
    } else {
        // add to end of existing list
        pwm = exported_pwms;
        while (pwm->next != NULL)
            pwm = pwm->next;
        pwm->next = new_pwm;
    }

    return 0;
}

int pwm_disable(const char *key)
{
    struct pwm_exp *pwm, *temp, *prev_pwm = NULL;
    char fragment[18];

    snprintf(fragment, sizeof(fragment), "bone_pwm_%s", key);
    fprintf(stderr, "pwm_disable fragment: %s\n", fragment);
    unload_device_tree(fragment);

    // remove from list
    pwm = exported_pwms;
    while (pwm != NULL)
    {
        if (strcmp(pwm->key, key) == 0)
        {
            //close the fd
            close(pwm->period_fd);
            close(pwm->duty_fd);
            if (prev_pwm == NULL)
                exported_pwms = pwm->next;
            else
                prev_pwm->next = pwm->next;
            temp = pwm;
            pwm = pwm->next;
            free(temp);
        } else {
            prev_pwm = pwm;
            pwm = pwm->next;
        }
    }
    return 0;    
}

int pwm_set_frequency(const char *key, float freq) {
    int len;
    char buffer[20];
    struct pwm_exp *pwm;

    if (freq <= 0.0)
        return -1;

    pwm = lookup_exported_pwm(key);

    pwm->period_ns = (unsigned long)(1e9 / freq);

    len = snprintf(buffer, sizeof(buffer), "%lu", pwm->period_ns);
    write(pwm->period_fd, buffer, len);

    return 1;
}

int pwm_set_duty_cycle(const char *key, float duty) {
    int len;
    char buffer[20];
    struct pwm_exp *pwm;

    if (duty < 0.0 || duty > 100.0)
        return -1;

    pwm = lookup_exported_pwm(key);

    pwm->duty = (unsigned long)(pwm->period_ns * duty);

    len = snprintf(buffer, sizeof(buffer), "%lu", pwm->duty);
    write(pwm->duty_fd, buffer, len);

    return 0;
}

void pwm_cleanup(void)
{
    while (exported_pwms != NULL) {
        fprintf(stderr, "pwm_clenaup key: %s\n", exported_pwms->key);
        pwm_disable(exported_pwms->key);
    }
}