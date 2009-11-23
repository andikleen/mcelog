struct config_choice {
	char *name;
	int val;
};

int config_choice(const char *header, const char *name, const struct config_choice *c);
char *config_string(const char *header, const char *name);
int config_number(const char *header, const char *name, char *fmt, void *val);
int config_bool(const char *header, const char *name);
int parse_config_file(const char *fn);
const char *config_file(char **av, const char *deffn);
struct option;
void config_options(struct option *opts, int (*func)(int));
struct bucket_conf;
int config_trigger(const char *header, const char *name, struct bucket_conf *bc);

