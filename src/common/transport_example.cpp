#include "simple_transport_demo.h"
#include "static_config.h"
#include <iostream>
#include <string>

namespace hft {

// Example of how the transport abstraction would be used
class TransportExample {
public:
    static void demonstrate_usage() {
        std::cout << "\n=== HFT Transport Interface Demonstration ===" << std::endl;
        
        // Get transport type from configuration
        std::string transport_config = StaticConfig::get_transport_type();
        auto transport_type = SimpleTransportFactory::parse_type_from_config(transport_config);
        
        std::cout << "Using transport type: " << SimpleTransportFactory::get_type_name(transport_type) << std::endl;
        
        // Example 1: Market data publisher
        demonstrate_market_data_transport(transport_type);
        
        // Example 2: Signal transport
        demonstrate_signal_transport(transport_type);
        
        std::cout << "Transport interface allows switching between ZeroMQ and SPMC without code changes!" << std::endl;
        std::cout << "=============================================" << std::endl;
    }

private:
    static void demonstrate_market_data_transport(SimpleTransportType type) {
        std::cout << "\n--- Market Data Transport Example ---" << std::endl;
        
        // Publisher side (market data handler)
        auto publisher = SimpleTransportFactory::create_publisher(type);
        if (publisher) {
            publisher->initialize(StaticConfig::get_market_data_endpoint());
            std::cout << "Market Data Publisher: " << publisher->get_endpoint() << std::endl;
            std::cout << "Transport Type: " << SimpleTransportFactory::get_type_name(publisher->get_type()) << std::endl;
        }
        
        // Subscriber side (strategy engine)
        auto subscriber = SimpleTransportFactory::create_subscriber(type);
        if (subscriber) {
            subscriber->initialize(StaticConfig::get_market_data_endpoint());
            std::cout << "Market Data Subscriber: " << subscriber->get_endpoint() << std::endl;
            std::cout << "Transport Type: " << SimpleTransportFactory::get_type_name(subscriber->get_type()) << std::endl;
        }
    }
    
    static void demonstrate_signal_transport(SimpleTransportType type) {
        std::cout << "\n--- Signal Transport Example ---" << std::endl;
        
        // Pusher side (strategy engine)
        auto pusher = SimpleTransportFactory::create_pusher(type);
        if (pusher) {
            pusher->initialize(StaticConfig::get_signals_endpoint());
            std::cout << "Signal Pusher: " << pusher->get_endpoint() << std::endl;
            std::cout << "Transport Type: " << SimpleTransportFactory::get_type_name(pusher->get_type()) << std::endl;
        }
        
        // Puller side (order gateway)
        auto puller = SimpleTransportFactory::create_puller(type);
        if (puller) {
            puller->initialize(StaticConfig::get_signals_endpoint());
            std::cout << "Signal Puller: " << puller->get_endpoint() << std::endl;
            std::cout << "Transport Type: " << SimpleTransportFactory::get_type_name(puller->get_type()) << std::endl;
        }
    }
};

} // namespace hft

// Demonstration function that can be called from any main()
void demonstrate_transport_interface() {
    hft::TransportExample::demonstrate_usage();
}

#ifdef STANDALONE_DEMO
int main() {
    demonstrate_transport_interface();
    return 0;
}
#endif