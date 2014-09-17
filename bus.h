void bus_setup(void);
void run_bus_trigger(int socket, int cpu, char *level, char *pp, char *rrrr,
		char *ii, char *timeout);
void run_iomca_trigger(int socket, int cpu, int seg, int bus, int dev, int fn);
