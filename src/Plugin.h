
#ifndef PLUGIN_H
#define PLUGIN_H


class Plugin {
    public:
        Plugin();
        ~Plugin();

         void load();
         
         void process(
            float* thrubuffer,
            int bufferSize
        );
};

#endif