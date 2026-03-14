#include <systemc.h>
// module named HelloWorld
SC_MODULE(HelloWorld){
    // Define a method to be called in the simulation
    void sayHello(){
        std::cout << "\033[1;36m"
                  << "method is called"
                  << "\033[0m" << std::endl;
}

// Constructor (module is created)
SC_CTOR(HelloWorld) {
    // Call the sayHello method when the module is instantiated
    std::cout << std::endl
              << "\033[1;35m"
              << "constructor called"
              << "\033[0m" << std::endl;
    SC_METHOD(sayHello);
}

// Destructor (module is destroyed)
~HelloWorld() {
    std::cout << "\033[1;35m"
              << "destructor called"
              << "\033[0m" << std::endl;
}
}
;

// Main function
int sc_main(int argc, char *argv[]) {
    // Instantiate the HelloWorld module
    HelloWorld hello("hello_world_instance");

    // Run the simulation
    sc_start();

    return 0;
}