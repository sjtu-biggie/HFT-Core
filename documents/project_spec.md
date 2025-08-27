

# **Project Plan: A Personal Portfolio Website and Low-Latency Trading System**

## **Executive Summary**

This document outlines a comprehensive project plan for the development and deployment of a dual-system platform. The primary objective is to construct a public-facing professional portfolio website that simultaneously serves as a control center for a private, high-performance, low-latency algorithmic trading system. This project is conceived as an advanced educational endeavor, designed to provide practical, industrial-grade experience in full-stack web development, ultra-low-latency systems engineering, and quantitative finance. The plan now defaults to professional-grade architectural principles and technologies from the outset.

The core deliverables of this project are fourfold:

1. A fully deployed, modern personal website accessible via a custom domain name.  
2. A modular, high-performance trading system, with its latency-critical components developed in C++, deployed on a dedicated bare-metal server to eliminate virtualization overhead.  
3. A high-fidelity backtesting framework capable of accurately simulating trading strategies against historical tick-by-tick data, accounting for critical factors like order queue position and network latency.  
4. An integrated, real-time web dashboard hosted on the personal website, providing comprehensive monitoring and control over the trading system's operations.

Key strategic decisions have been made to ensure the project's success within its technical and financial parameters. The frontend website will be built using the **Astro** framework, leveraging its "islands architecture" to host a dynamic dashboard built with **React** components. This approach ensures a high-performance static site while accommodating complex, real-time user interface requirements.1 The high-frequency trading (HFT) system will be architected as a set of decoupled microservices, using modern

**C++** for performance-critical components and **ZeroMQ** for lightweight, high-speed inter-process communication.3 To achieve the lowest possible latency, the system will employ

**kernel-bypass networking** using the **Data Plane Development Kit (DPDK)**, allowing the market data handler to ingest data directly from the network interface card (NIC), avoiding the overhead of the operating system's kernel.131 For other high-performance I/O tasks, such as logging, the system will utilize

**io\_uring** to minimize system call overhead.134 To facilitate realistic yet affordable operation, the system will interface with

**Alpaca** for brokerage services and **Polygon.io** for market data, chosen for their developer-friendly, low-cost APIs.5 Deployment will be on a

**bare-metal server**, prioritizing raw performance and predictable latency over the flexibility of cloud virtual machines.137

The project roadmap is structured into distinct, iterative phases. It begins with the foundational website, proceeds with the incremental development of the HFT system's components and backtester, and culminates in their integration, bare-metal deployment, and end-to-end testing. This phased approach prioritizes the early establishment of a functional system, which can be progressively refined and enhanced over time.

---

## **Part I: The Public Showcase \- Your Personal Website**

This initial part of the project focuses on establishing a robust and professional online presence. Given the user's lack of prior web development experience, the plan emphasizes modern tools and frameworks that simplify the development process while delivering a high-quality, performant, and visually appealing final product.

### **1.1 Framework Selection: Astro for Performance and Flexibility**

The choice of a web framework is a foundational decision that impacts performance, developer experience, and cost. For this project, a static-first approach is paramount.

Primary Recommendation: Astro  
Astro is a modern static site generator that is exceptionally well-suited for content-heavy websites such as portfolios, blogs, and marketing sites.2 Its core philosophy is "zero JavaScript by default," meaning it renders components to static HTML at build time, shipping the absolute minimum amount of client-side JavaScript necessary. This results in websites that load with remarkable speed, a critical factor for user retention and creating a professional first impression.2  
The most compelling feature of Astro for this project is its "Islands Architecture".2 This innovative approach allows developers to build a website that is predominantly static HTML, while specific, designated components—or "islands"—can be rendered as fully interactive client-side applications using popular UI frameworks like React or Vue. This hybrid model is perfectly aligned with the project's requirements: a largely static portfolio site that contains a highly dynamic, real-time trading dashboard. The portfolio pages remain lightweight and fast, while the dashboard island receives the necessary JavaScript to become a full-featured application.

Comparative Analysis: Astro vs. Next.js  
Next.js is a powerful and popular full-stack framework based on React, widely used for building dynamic web applications.7 While it is an excellent choice for complex, server-driven applications, its default reliance on client-side rendering and hydration can be suboptimal for a content-focused portfolio. This can lead to larger initial JavaScript bundles and potentially slower page loads compared to Astro's static-first approach.1 For the specific use case of a mostly static site hosting a single, complex real-time component, Astro offers a more optimized balance of performance and capability, avoiding the overhead of a full single-page application (SPA) where it is not needed.7  
The selection of a static-first framework like Astro is not merely a technical preference; it is a strategic decision that directly enables the project to meet its primary financial constraint. Modern hosting platforms such as Vercel and Netlify offer exceptionally generous free tiers for static websites, which include features like a global CDN, automatic HTTPS, and continuous deployment.13 A framework that generates a truly static site can fully leverage these free tiers. In contrast, frameworks that require a server runtime for features like server-side rendering (SSR) often push projects into paid hosting plans more quickly. Therefore, the choice of Astro allows the project to be deployed on Vercel's free tier, effectively reducing the website's static hosting cost to $0 and ensuring adherence to the project's strict budget.

### **1.2 Core Website Implementation: A Step-by-Step Guide**

This section provides a clear path for building the website from the ground up.

* **Environment Setup:** The first step is to prepare the local development environment. This involves installing Node.js (which includes the npm package manager) and a code editor like Visual Studio Code. Recommended VS Code extensions, such as the official Astro extension and Tailwind CSS IntelliSense, will significantly improve the development experience by providing syntax highlighting and autocompletion.18  
* **Project Initialization:** The project begins by running the npm create astro@latest command in the terminal. This command launches a setup wizard that guides the user through the initial project configuration.19 For this project, selecting the "Empty" template is recommended, as it provides a minimal starting point that is ideal for learning the framework's core concepts from scratch.  
* **Creating Pages and Routing:** Astro employs an intuitive file-based routing system. Any .astro file created within the src/pages/ directory automatically becomes a page on the website.9 The plan involves creating the essential pages for a professional portfolio:  
  * src/pages/index.astro: The homepage.  
  * src/pages/about.astro: A page for a personal biography and skills.  


## **Part IV: Implementation: Technology, Data, and Strategy**

This section delves into the specific technologies, data sources, and algorithmic logic that will bring the trading system to life. It emphasizes modern C++ practices, cost-effective data acquisition, and a well-defined starter trading strategy.

### **4.1 The C++ Advantage: Modern Techniques for Low Latency**

To achieve the performance required for an HFT system, the implementation will leverage modern C++ features and low-level optimization techniques.

* **Language Standard:** The project will be built using the **C++20** standard. This provides access to modern language features like concepts, ranges, and coroutines, which can lead to safer, more expressive, and more maintainable code without compromising the zero-overhead principle that is critical for performance.3  
* **Performance-Critical Techniques:**  
  * **Memory Management:** On the "hot path," dynamic memory allocation via new and delete will be strictly avoided. These operations can introduce non-deterministic latency due to heap contention and system calls. Instead, the system will favor stack allocation for temporary objects, pre-allocation of memory pools for frequently created objects, and using containers like std::vector that allocate contiguous memory blocks upfront.66  
  * **Cache Locality:** Modern CPUs are orders of magnitude faster than main memory; therefore, efficient use of the CPU cache is paramount. The system's data structures will be designed for cache-friendliness. This means preferring contiguous-memory containers like std::vector or std::array over node-based containers like std::list or std::map for data that is accessed sequentially or frequently. This principle, known as cache locality, ensures that when the CPU fetches one piece of data, adjacent data is also loaded into the cache, reducing the chance of a slow "cache miss" on subsequent accesses.66  
  * **Compiler Optimizations:** The code will be written to be "compiler-friendly," enabling maximum optimization. This includes using constexpr for compile-time computations, judicious use of inline for small, frequently called functions to eliminate function call overhead, and structuring code to minimize conditional branches, which can lead to costly pipeline flushes due to branch misprediction.70  
  * **Concurrency:** Within a single service, communication between threads must be highly efficient. The design will utilize lock-free data structures, such as a single-producer, single-consumer (SPSC) queue, for passing data between threads. This avoids the use of mutexes, which can cause thread suspension and context switching managed by the operating system kernel, introducing significant latency. The Disruptor pattern, a high-performance alternative to traditional queues, is a well-regarded design for this purpose.66

### **4.2 Sourcing Market Data on a Budget**

Access to real-time, tick-by-tick market data is one of the most significant cost barriers in institutional trading.41 This project requires a practical, low-cost solution that provides a realistic development and testing environment.

* **Recommended Solution: A Hybrid Approach**  
  * **Live/Paper Trading Data: Alpaca API.** Alpaca offers a free, real-time market data feed sourced from the Investors Exchange (IEX).73 While IEX only accounts for a fraction of the total U.S. market volume, its data is sufficient for developing, testing, and running a live paper trading system. Crucially, this data is delivered via a modern  
    **WebSocket** stream, which is the industry-standard protocol for receiving continuous, low-latency real-time updates.75  
  * **Historical/Backtesting Data: Polygon.io API.** For strategy development and backtesting, historical data is required. Polygon.io provides a free tier that includes access to historical end-of-day data and a limited number of API calls per minute for more granular historical data.6 This is ideal for downloading the datasets needed to perform the offline analysis for the pairs trading strategy.

| Market Data Provider | Data Type | Coverage | Cost (Free Tier) | API Type | Limitations |
| :---- | :---- | :---- | :---- | :---- | :---- |
| **Alpaca** | Real-time & Historical | IEX (Free), SIP (Paid) | Free tier includes unlimited IEX data | REST & WebSocket | Free tier data is from a single exchange (IEX) 74 |
| **Polygon.io** | Real-time & Historical | Full US Market (SIP) | Free tier: 5 API calls/min, delayed data | REST & WebSocket | Free tier is rate-limited and data is delayed 6 |
| **Finnhub** | Real-time & Historical | Global Stocks, Forex, Crypto | Free tier: 60 API calls/min | REST & WebSocket | Limited API calls on free tier 80 |
| **FMP** | Real-time & Historical | US Stocks, Forex, Crypto | Free tier: 250 API calls/day | REST | Very limited API calls on free tier 81 |

The combination of Alpaca for real-time streaming and Polygon.io for historical data provides a comprehensive and cost-effective solution that covers all the project's data needs without incurring significant monthly fees.

### **4.3 Brokerage Connectivity: Alpaca vs. Interactive Brokers (IBKR)**

The choice of brokerage API directly influences development complexity and the system's capabilities.

* **Primary Recommendation: Alpaca.** Alpaca is an API-first brokerage built specifically for developers and algorithmic traders.5 Its API is based on modern web standards (REST for requests, WebSocket for streaming updates), making it significantly easier to integrate into a custom application compared to legacy APIs.73 It offers commission-free trading on US stocks and a fully-featured paper trading environment that uses a separate set of API keys but mirrors the live trading API, which is invaluable for safe development and testing.85  
* **Alternative: Interactive Brokers (IBKR).** IBKR is a professional-grade broker offering access to a vast range of global markets and financial instruments.88 Its TWS API is powerful but also notoriously complex. It requires a "gateway" application (either the Trader Workstation or a headless gateway) to be running on the machine, and it uses a proprietary binary protocol, which presents a steeper learning curve.91 While a C++ API is available, community feedback suggests that integration can be challenging.91

For a personal project where the primary goal is to build a functional end-to-end system, minimizing development friction is more important than having access to every global market. The complexity of the IBKR API could easily derail the project, consuming valuable time that would be better spent on the core trading logic and system architecture. Alpaca's modern API represents the "path of least resistance" to achieving a working Order Gateway. Therefore, the project will begin with an Alpaca integration. The Order Gateway component will be designed around an abstract Brokerage interface, ensuring that an InteractiveBrokers adapter could be developed and added in the future as an enhancement, without requiring a rewrite of the entire system.

### **4.4 A Starter Strategy: Statistical Arbitrage (Pairs Trading)**

The system requires an initial trading strategy to implement and test. Pairs trading is an ideal choice as it is a well-documented, market-neutral strategy based on quantifiable statistical relationships.

* **Strategy Rationale:** Pairs trading is a form of statistical arbitrage that profits from the temporary divergence in price between two historically correlated assets, betting on their eventual convergence or "mean reversion".95 Its logic is purely quantitative, making it well-suited for algorithmic implementation.  
* **Implementation Steps:**  
  1. **Pair Selection (Offline Analysis):** This step is performed offline using historical data.  
     * Data acquisition: Use the Polygon.io API to download several years of historical daily price data for a universe of potentially related stocks (e.g., components of the S\&P 500, or stocks within the same sector like Coca-Cola and PepsiCo).  
     * Candidate identification: Identify potential pairs by finding stocks with a high correlation coefficient or a small sum of squared deviations between their normalized price series.98  
     * Statistical validation: The most critical step is to test for **cointegration** using the **Engle-Granger two-step test**.100 This test statistically validates that a long-term equilibrium relationship exists between the two price series. It involves two stages:  
       1. Run a linear regression on the log-prices of the two stocks (e.g., log(price\_A)=β⋅log(price\_B)+α).  
       2. Perform an Augmented Dickey-Fuller (ADF) test on the residuals (the error term) of this regression. If the residuals are stationary (i.e., the ADF test rejects the null hypothesis of a unit root), the pair is considered cointegrated.103  
  2. **Signal Generation (Real-time in Strategy Engine):**  
     * For a selected cointegrated pair, the Strategy Engine continuously calculates the spread in real-time using the formula: Spreadt​=log(price\_At​)−β⋅log(price\_Bt​), where β is the hedge ratio determined from the initial regression.  
     * The engine then normalizes this spread by calculating its rolling **Z-score**: Z−score=(current\_spread−rolling\_mean\_of\_spread)/rolling\_std\_dev\_of\_spread.99 The Z-score measures how many standard deviations the current spread is from its recent average.  
     * **Entry/Exit Logic:** Trading signals are generated based on thresholds for the Z-score.  
       * **Entry Long Spread:** If Z−score\<−2.0, the spread is considered unusually low (A is undervalued relative to B). The signal is to "buy the spread": buy stock A and short-sell β shares of stock B.  
       * **Entry Short Spread:** If Z−score\>+2.0, the spread is unusually high (A is overvalued relative to B). The signal is to "short the spread": short-sell stock A and buy β shares of stock B.  
       * **Exit:** If a position is open and the Z-score reverts towards its mean (e.g., ∣Z−score∣\<0.5), the signal is to close both legs of the trade to realize the profit.106  
* **Risk Management:**  
  * **Position Sizing:** The capital allocated to each trade will be determined by a simple rule, such as a fixed percentage of the total portfolio value (e.g., risk no more than 2% of capital on any single pairs trade).55  
  * **Stop-Loss:** A critical risk control is to implement a stop-loss based on the spread itself. If the Z-score continues to diverge and reaches an extreme level (e.g., ±3.0 standard deviations), the position is automatically closed. This protects against catastrophic losses in the event that the historical relationship between the pair breaks down permanently.113

---

## **Part V: The Command Center \- Visualization and Control**

This part details the creation of the user-facing dashboard, which serves as the primary interface for monitoring and interacting with the trading system. The guiding architectural principle is the strict separation of the user interface from the core trading engine to guarantee performance and stability.

### **5.1 Frontend Technology: React as an Astro Island**

The dashboard requires a technology stack capable of handling real-time data streams and providing a rich, interactive user experience.

* **Framework Choice:** **React** is the industry standard for building complex, component-based user interfaces and is an excellent choice for a data-intensive dashboard.10 To optimize performance for the overall website, the React dashboard will be implemented as an  
  **Astro island**. This means the React application will only be loaded on the specific dashboard page, using the client:load directive in Astro. This prevents the React library from being included on the static portfolio pages, keeping them lightweight and fast.  
* **Data Visualization:** A specialized charting library is essential for visualizing financial data effectively. **Lightweight Charts by TradingView** is a high-performance, open-source library specifically designed for financial charting, making it an ideal choice. Alternatives like Recharts or Nivo are powerful but are more general-purpose and may require more customization for financial use cases.117  
* **Component Libraries:** To accelerate the development of the UI, a pre-built component library such as **Shadcn/UI** or **Material-UI (MUI)** will be used. These libraries provide a comprehensive set of ready-to-use components like buttons, tables, modals, and layout grids, allowing the focus to remain on the dashboard's functionality rather than building basic UI elements from scratch.118

### **5.2 Backend-to-Frontend Communication: The WebSocket Bridge**

A robust and efficient communication channel is needed to stream data from the C++ backend to the browser-based dashboard.

* **The Challenge:** The C++ microservices communicate internally using ZeroMQ, a protocol that is not natively supported by web browsers. Therefore, a bridge is required to translate the internal message bus traffic into a web-friendly format.  
* **Architecture:** A dedicated **WebSocket Bridge** service will be implemented. This service will be a lightweight application (which can be written in Python or Node.js for simplicity, or C++ using a library like uWebSockets for maximum performance). Its functions are:  
  1. Act as a ZeroMQ subscriber, connecting to the internal message bus and listening for all relevant events (e.g., P\&L updates from the Position Service, execution reports from the Order Gateway, status updates from all services).  
  2. Host a WebSocket server. When a frontend client (the React dashboard) connects, the bridge will broadcast the received ZeroMQ messages to it over the WebSocket connection.  
* **Data Flow:** This architecture creates a one-way, real-time data stream from the core trading system to the dashboard. This design is critical for system integrity: the UI becomes a passive observer that cannot block or interfere with the performance of the latency-sensitive trading engine.76

The user's requirement for a dashboard that is "completely separate from the trading engine to ensure it cannot interfere with trading performance" is a professional-grade concern that dictates a specific architectural solution.41 A direct connection from the frontend to the core C++ components would create tight coupling and introduce potential performance risks; a slow network connection or a bug in the UI could block a critical trading thread. The WebSocket Bridge pattern enforces this separation by acting as an intermediary. It decouples the frontend's communication protocol (WebSockets) from the backend's (ZeroMQ). The trading engine remains entirely unaware of any UI clients; it simply publishes status messages to its internal bus. This architectural choice is a direct and robust solution to a primary user requirement, guaranteeing the integrity and performance of the trading core.

### **5.3 The Control Panel: Sending Commands to the Backend**

While data flow to the dashboard is one-way, the user also needs the ability to send commands to the system.

* **Architecture:** For control actions—such as "START TRADING," "STOP TRADING," or "LIQUIDATE ALL POSITIONS"—a simple **REST API** will be exposed by a new **Control API** service. This service will be a lightweight C++ application built using a modern C++ web framework like **Drogon** or **Boost.Beast**.123  
* **Data Flow:** The React dashboard will send secure HTTP requests (e.g., a POST request to /api/start) to the Control API. The Control API service will validate the request and then publish a corresponding command message onto the ZeroMQ bus. The relevant backend services (like the Strategy Engine or Order Gateway) will subscribe to these command messages and execute the requested action.  
* **Security:** This API endpoint is a critical control surface and must be secured. At a minimum, it will require an API key in the request headers. It should be configured on the server's firewall to only accept connections from the web server itself (localhost), preventing direct public access.

### **5.4 UI/UX for Trading Dashboards**

The design of the dashboard must prioritize clarity, information hierarchy, and actionable insights, allowing the user to understand the system's state at a glance.126

* **Key Principles:** The design will follow established UI/UX best practices. The most critical information will be placed at the top-left of the screen, following the natural "F-pattern" of how users scan web pages.128 Visual cues like color (green for positive P\&L/running status, red for negative P\&L/errors) will be used to convey information quickly.129  
* **Dashboard Views:** The dashboard will be organized into logical sections or tabs:  
  * **System Health View:** This view provides an operational overview. It will display the status of each microservice (e.g., a green light for "Connected," red for "Error"), the server's CPU and memory utilization, and a real-time log feed from the backend services.  
  * **Trading View:** This is the primary financial dashboard. It will feature a real-time chart of the strategy's equity curve, key performance indicators (KPIs) such as daily and total P\&L, a table of current open positions, and a log of recent trade executions.  
  * **Control Panel View:** This view provides the user with manual control over the system. It will include clear, well-labeled buttons to start and stop the trading strategy, adjust key risk parameters (like maximum position size), and a prominent "emergency stop" button that immediately liquidates all open positions and halts trading.53

---

## **Part VI: The Master Plan \- From Concept to Execution**

This final part consolidates the entire project into a phased roadmap, provides a detailed infrastructure and cost analysis, and presents a step-by-step plan suitable for implementation by a coding agent.

### **6.1 Phased Implementation Roadmap**

This roadmap is structured to deliver value incrementally, ensuring that a testable and functional system is available at the conclusion of each phase.

* **Phase 1: Website Foundation (1-2 weeks)**  
  1. Set up the local development environment (Node.js, VS Code).  
  2. Initialize a new Astro project using npm create astro@latest and add Tailwind CSS.  
  3. Build the static pages: Home, About, and a Projects landing page.  
  4. Create a reusable Layout.astro component for the site's header and footer.  
  5. Initialize a Git repository and push the project to GitHub.  
  6. Deploy the initial static site to Vercel by importing the GitHub repository.  
  7. Purchase a custom domain name from Cloudflare and connect it to the Vercel project by updating nameservers.  
  * **Goal:** A live, public-facing personal website is operational.  
* **Phase 2: HFT Backend Scaffolding & Low-Latency I/O (3-4 weeks)**  
  1. Set up the C++ development environment on a local Linux machine (g++, CMake, Boost, ZeroMQ, DPDK, liburing).  
  2. Define the internal message structures for communication (e.g., using simple structs or Protocol Buffers for serialization).  
  3. Implement the basic skeletons for all C++ services as separate executables.  
  4. Implement a proof-of-concept MarketDataHandler using DPDK to capture raw packets from a dedicated NIC and print them.  
  5. Implement a proof-of-concept Low-Latency Logger that writes messages to a file using io\_uring.  
  6. Establish the ZeroMQ PUB/SUB and PUSH/PULL communication channels between the services.  
  7. Conduct an end-to-end test using mock data generators to verify that messages flow correctly through the system.  
  * **Goal:** A functioning, decoupled backend system with verified inter-process communication and successful proof-of-concepts for kernel-bypass networking and asynchronous disk I/O.  
* **Phase 3: Data, Brokerage, and Backtester Integration (3-4 weeks)**  
  1. Sign up for developer accounts with Alpaca and Polygon.io to obtain API keys.  
  2. Implement the full MarketDataHandler logic to parse live market data protocols.  
  3. Implement the Alpaca REST/WebSocket client within the Order Gateway using a user-space network stack.  
  4. Test the ability to place paper trading orders and receive execution updates from Alpaca.  
  5. Develop a Python script to download historical tick data from Polygon.io and save it to CSV files.  
  6. Implement the Historical Data Player component for the backtester to read and publish historical data.  
  7. Implement the Fill Simulator module, including logic for order queue position estimation.  
  * **Goal:** The HFT system can process live data and interact with a paper trading account. The core components of the backtesting framework are functional.  
* **Phase 4: Strategy Implementation & Backtesting (2-3 weeks)**  
  1. Implement the full Pairs Trading strategy logic within the Strategy Engine.  
  2. Develop the offline Python analysis scripts to perform the Engle-Granger test and identify cointegrated stock pairs.  
  3. Run the complete backtester on a selected pair to validate the strategy's historical performance, generating a full performance report.  
  4. Iterate on strategy parameters based on backtesting results to optimize performance.  
  * **Goal:** A fully backtestable trading strategy with quantifiable and optimized performance metrics.  
* **Phase 5: Frontend Dashboard & Integration (3-4 weeks)**  
  1. Create the React components for the dashboard within the Astro project, placing them in an Astro island.  
  2. Implement the WebSocket Bridge and Control API services.  
  3. Connect the React dashboard to the WebSocket Bridge to display real-time data streams.  
  4. Implement control buttons in the dashboard UI that send HTTP requests to the Control API.  
  5. Design and build the three primary dashboard views: System Health, Trading, and Control Panel.  
  * **Goal:** A fully interactive, real-time dashboard is integrated with the backend system, providing both monitoring and control capabilities.  
* **Phase 6: Bare-Metal Deployment & End-to-End Testing (2-3 weeks)**  
  1. Provision a bare-metal server in a data center strategically located for low latency to exchange servers (e.g., Chicago for CME).  
  2. Configure the server's Linux OS for low-latency performance (e.g., kernel tuning, CPU isolation, disabling unnecessary services).  
  3. Install the C++ toolchain and all necessary dependencies (DPDK, etc.).  
  4. Create deployment scripts (e.g., using Docker and GitHub Actions) to automate building and running the C++ services on the server.  
  5. Configure server networking and firewall rules to securely expose the WebSocket and Control API endpoints.  
  6. Update the frontend application with the public IP address of the deployed backend services.  
  7. Conduct a comprehensive end-to-end test of the entire live system.  
  * **Goal:** The complete, integrated project is live, operational on bare-metal hardware, and accessible via the public internet.

### **6.2 Infrastructure, Deployment, and Cost Optimization**

To meet the user's request for the highest possible performance, the infrastructure will be based on a bare-metal server, which eliminates the "noisy neighbor" problem and performance unpredictability associated with virtualized cloud environments.137

* **Bare-Metal Provider Selection for HFT Backend:**  
  * **Requirements:** A dedicated server with a modern, high-frequency CPU (e.g., AMD Ryzen), NVMe SSDs, and a high-speed network interface. For trading US futures, a server located in a Chicago data center is ideal for minimizing latency to the CME exchange.154  
  * **Comparison:**

| Provider | Location Focus | CPU Examples | Est. Monthly Cost | Notes |
| :---- | :---- | :---- | :---- | :---- |
| **Cherry Servers** | Chicago, EU | AMD Ryzen 7700X/9900X | $158+ | High-performance AMD CPUs, flexible billing 154 |
| **OVHcloud** | Global | Various Intel/AMD | $65+ | Wide range of options, good value 158 |
| **QuantVPS** | Chicago | AMD Ryzen | $299+ (Dedicated) | Specialized for traders, co-located with CME 156 |
| **HostVenom** | Chicago | Intel/AMD | Custom Pricing | Premium network with direct exchange connections 159 |

\*   \*\*Recommendation:\*\* For a balance of cost and performance for this project, \*\*Cherry Servers\*\* or \*\*OVHcloud\*\* are excellent starting points. They offer modern hardware in key financial locations at a more accessible price point than highly specialized trading hosts.

* **Deployment Automation:**  
  * **Containerization:** To ensure a consistent and reproducible deployment environment, all backend C++ applications and the bridge/API services will be containerized using **Docker**. This encapsulates all dependencies within a portable image, simplifying the deployment process.130  
  * **CI/CD for Backend:** A basic CI/CD pipeline will be established using **GitHub Actions**. On every push to the main branch, a GitHub Actions workflow will automatically build the Docker images for each service and push them to a container registry (e.g., Docker Hub or GitHub Container Registry). A simple deployment script on the server can then be run to pull the latest images and restart the services.  
* **Final Budget Analysis:**  
  * **Static Costs (Monthly):**  
    * Domain Name Registration: \~$1.25/month (based on $15/year).27  
    * Website Hosting (Vercel): $0 (Hobby Tier).13  
    * HFT Server (OVHcloud Bare Metal): \~$65/month (starting price).158  
    * **Total Estimated Static Cost: \~$66.25/month.**  
  * **Budgetary Note:** The shift to a bare-metal server to accommodate the request for kernel-bypass and other industrial-grade optimizations places the static cost slightly above the original $50/month target. This reflects the trade-off between the cost of consumer-grade cloud VMs and the higher performance and predictability of dedicated hardware, which is a prerequisite for ultra-low-latency trading.137  
  * **Active Running Costs (Hourly):**  
    * Data API Calls: The free tiers of Alpaca and Polygon.io should be sufficient for this project's needs, resulting in no direct cost.  
    * Cloud Server Costs: This is a fixed monthly cost, not hourly.  
    * Brokerage Commissions: $0 when using Alpaca for live trading.  
    * **Total Estimated Active Cost: \~$0/hour.** The primary operational cost is the fixed server fee, which is well below the user's \<$5/hour active running rate target.

### **6.3 Conclusion and Future Trajectory**

This project plan outlines a clear and achievable path to building a sophisticated, dual-purpose platform that combines a modern web presence with a high-performance algorithmic trading system. By adhering to this plan, the developer will gain invaluable hands-on experience in a wide range of in-demand technologies and concepts, including full-stack web development with Astro and React, low-latency C++ programming, microservices architecture with ZeroMQ, quantitative strategy development, and bare-metal server deployment and optimization.

Upon successful completion, the project will serve as a powerful portfolio piece and a robust foundation for further exploration. Potential future enhancements include:

* **Strategy Expansion:** Implementing more complex trading strategies, such as market making or other forms of statistical arbitrage, within the modular Strategy Engine.  
* **Hardware Acceleration:** Offloading the most latency-critical parts of the trading logic (such as parsing market data or pre-trade risk checks) from the CPU to an FPGA for nanosecond-level execution.41  
* **Live Trading:** After extensive and successful paper trading, transitioning the Order Gateway to a live-funded Alpaca account to trade with real capital.  
* **Brokerage Expansion:** Developing an InteractiveBrokers adapter for the Order Gateway to gain access to a wider array of markets, including options and futures.  
* **Advanced Dashboard Features:** Enhancing the UI with more detailed analytics, historical performance charting, and the ability to tune strategy parameters directly from the control panel.

#### **Works cited**

1. Why We Transitioned: A Comparison of Next.js vs Astro | by Mina Stankovic | Medium, accessed August 21, 2025, [https://medium.com/@stankovicd.mina/why-we-transitioned-a-comparison-of-next-js-vs-astro-ab8fcc839484](https://medium.com/@stankovicd.mina/why-we-transitioned-a-comparison-of-next-js-vs-astro-ab8fcc839484)  
2. Next.js vs Astro vs Remix: Choosing the Right Front-end Framework | by Solomon Eseme, accessed August 21, 2025, [https://medium.com/strapi/next-js-vs-astro-vs-remix-choosing-the-right-front-end-framework-59f0e74c9d8e](https://medium.com/strapi/next-js-vs-astro-vs-remix-choosing-the-right-front-end-framework-59f0e74c9d8e)  
3. Modern C++ in Finance. Building Low-Latency, High-Reliability Systems \- Scythe Studio, accessed August 21, 2025, [https://scythe-studio.com/en/blog/modern-c-in-finance-building-low-latency-high-reliability-systems](https://scythe-studio.com/en/blog/modern-c-in-finance-building-low-latency-high-reliability-systems)  
4. Get started \- ZeroMQ, accessed August 21, 2025, [https://zeromq.org/get-started/](https://zeromq.org/get-started/)  
5. Alpaca \- Developer-first API for Stock, Options, Crypto Trading, accessed August 21, 2025, [https://alpaca.markets/](https://alpaca.markets/)  
6. Pricing \- Polygon.io, accessed August 21, 2025, [https://polygon.io/pricing](https://polygon.io/pricing)  
7. Astro vs NextJS 2025 : Comparison, Features \- Aalpha Information Systems, accessed August 21, 2025, [https://www.aalpha.net/blog/astro-vs-nextjs-comparison/](https://www.aalpha.net/blog/astro-vs-nextjs-comparison/)  
8. Hugo vs Astro vs NextJs \- which one is better for content-focused website? \- support, accessed August 21, 2025, [https://discourse.gohugo.io/t/hugo-vs-astro-vs-nextjs-which-one-is-better-for-content-focused-website/42858](https://discourse.gohugo.io/t/hugo-vs-astro-vs-nextjs-which-one-is-better-for-content-focused-website/42858)  
9. Creating an Astro Site: Beginners' Tutorial | CloudCannon, accessed August 21, 2025, [https://cloudcannon.com/tutorials/astro-beginners-tutorial-series/](https://cloudcannon.com/tutorials/astro-beginners-tutorial-series/)  
10. Top 7 Frontend Frameworks to Use in 2025: Pro Advice \- Developer Roadmaps, accessed August 21, 2025, [https://roadmap.sh/frontend/frameworks](https://roadmap.sh/frontend/frameworks)  
11. Please help with choosing Between Astro and Next.js for a Web Development Agency : r/webdev \- Reddit, accessed August 21, 2025, [https://www.reddit.com/r/webdev/comments/1hox0ik/please\_help\_with\_choosing\_between\_astro\_and/](https://www.reddit.com/r/webdev/comments/1hox0ik/please_help_with_choosing_between_astro_and/)  
12. Astro vs Next.js: Which is the Best Web Framework? \- YouTube, accessed August 21, 2025, [https://www.youtube.com/watch?v=55i5LcrY6nU](https://www.youtube.com/watch?v=55i5LcrY6nU)  
13. Limits \- Vercel, accessed August 21, 2025, [https://vercel.com/docs/limits](https://vercel.com/docs/limits)  
14. 11 Best Static Web Hosting Services (2025) \- HostingAdvice.com, accessed August 21, 2025, [https://www.hostingadvice.com/how-to/best-static-web-hosting/](https://www.hostingadvice.com/how-to/best-static-web-hosting/)  
15. Free Static Site Hosting \- Wasmer, accessed August 21, 2025, [https://wasmer.io/static-site-hosting](https://wasmer.io/static-site-hosting)  
16. Static.app: Static Website Hosting, accessed August 21, 2025, [https://static.app/](https://static.app/)  
17. Vercel Hobby Plan, accessed August 21, 2025, [https://vercel.com/docs/plans/hobby](https://vercel.com/docs/plans/hobby)  
18. Getting started with Astro Framework \- Refine dev, accessed August 21, 2025, [https://refine.dev/blog/astro-js-guide/](https://refine.dev/blog/astro-js-guide/)  
19. Build a blog tutorial \- Astro Docs, accessed August 21, 2025, [https://docs.astro.build/en/tutorial/0-introduction/](https://docs.astro.build/en/tutorial/0-introduction/)  
20. Getting started | Docs \- Astro, accessed August 21, 2025, [https://docs.astro.build/en/getting-started/](https://docs.astro.build/en/getting-started/)  
21. Build a blog tutorial: Create your first Astro project | Docs, accessed August 21, 2025, [https://docs.astro.build/en/tutorial/1-setup/2/](https://docs.astro.build/en/tutorial/1-setup/2/)  
22. Tutorial: Building a Website with Astro Build \- DEV Community, accessed August 21, 2025, [https://dev.to/irishgeoffrey/tutorial-building-a-website-with-astro-build-28nc](https://dev.to/irishgeoffrey/tutorial-building-a-website-with-astro-build-28nc)  
23. What is Astro? A Step-by-Step Tutorial for Beginners in 2025 \- Themefisher, accessed August 21, 2025, [https://themefisher.com/astro-js-introduction](https://themefisher.com/astro-js-introduction)  
24. Build a blog tutorial: Create your first Astro page | Docs, accessed August 21, 2025, [https://docs.astro.build/en/tutorial/2-pages/1/](https://docs.astro.build/en/tutorial/2-pages/1/)  
25. 37 Best Web Development Frameworks to Use in 2025 \- Global Media Insight, accessed August 21, 2025, [https://www.globalmediainsight.com/blog/web-development-frameworks/](https://www.globalmediainsight.com/blog/web-development-frameworks/)  
26. Cloudflare Registrar | Domain Registration & Renewal, accessed August 21, 2025, [https://www.cloudflare.com/products/registrar/](https://www.cloudflare.com/products/registrar/)  
27. How much does a domain name cost? Find out\! \- GoDaddy, accessed August 21, 2025, [https://www.godaddy.com/resources/skills/how-much-domain-name-cost](https://www.godaddy.com/resources/skills/how-much-domain-name-cost)  
28. How Much Does a Domain Name Cost In 2025? \- Wix.com, accessed August 21, 2025, [https://www.wix.com/blog/how-much-does-a-domain-name-cost](https://www.wix.com/blog/how-much-does-a-domain-name-cost)  
29. Astro on Vercel, accessed August 21, 2025, [https://vercel.com/docs/frameworks/astro](https://vercel.com/docs/frameworks/astro)  
30. How to Deploy Projects on Vercel with GitHub: A Beginner's Guide \- JHK Infotech, accessed August 21, 2025, [https://www.jhkinfotech.com/blog/how-to-deploy-projects-on-vercel-with-github](https://www.jhkinfotech.com/blog/how-to-deploy-projects-on-vercel-with-github)  
31. Deploying GitHub Projects with Vercel, accessed August 21, 2025, [https://vercel.com/docs/git/vercel-for-github](https://vercel.com/docs/git/vercel-for-github)  
32. Fair use Guidelines \- Vercel, accessed August 21, 2025, [https://vercel.com/docs/limits/fair-use-guidelines](https://vercel.com/docs/limits/fair-use-guidelines)  
33. From Code to Live in Minutes: Deploying My Astro Starlight Static Site on Vercel., accessed August 21, 2025, [https://dev.to/uzukwu\_michael\_91a95b823b/from-code-to-live-in-minutes-deploying-my-astro-starlight-static-site-on-vercel-49ca](https://dev.to/uzukwu_michael_91a95b823b/from-code-to-live-in-minutes-deploying-my-astro-starlight-static-site-on-vercel-49ca)  
34. How do I add a custom domain to my Vercel project?, accessed August 21, 2025, [https://vercel.com/guides/how-do-i-add-a-custom-domain-to-my-vercel-project](https://vercel.com/guides/how-do-i-add-a-custom-domain-to-my-vercel-project)  
35. Working with domains \- Vercel, accessed August 21, 2025, [https://vercel.com/docs/domains/working-with-domains](https://vercel.com/docs/domains/working-with-domains)  
36. Use an existing domain \- Vercel, accessed August 21, 2025, [https://vercel.com/docs/getting-started-with-vercel/use-existing](https://vercel.com/docs/getting-started-with-vercel/use-existing)  
37. Add a domain \- Vercel, accessed August 21, 2025, [https://vercel.com/docs/getting-started-with-vercel/domains](https://vercel.com/docs/getting-started-with-vercel/domains)  
38. Buy a domain \- Vercel, accessed August 21, 2025, [https://vercel.com/docs/getting-started-with-vercel/buy-domain](https://vercel.com/docs/getting-started-with-vercel/buy-domain)  
39. Adding & Configuring a Custom Domain \- Vercel, accessed August 21, 2025, [https://vercel.com/docs/domains/working-with-domains/add-a-domain](https://vercel.com/docs/domains/working-with-domains/add-a-domain)  
40. How to Purchase Domain in Vercel, Create subdomain, and Connect with Third-Party Domains. \- YouTube, accessed August 21, 2025, [https://www.youtube.com/watch?v=b2Sl1L2b4G0](https://www.youtube.com/watch?v=b2Sl1L2b4G0)  
41. Headlands-Quant-Trading-Summary-Max-Dama.pdf  
42. Inside a Real High-Frequency Trading System | HFT Architecture \- YouTube, accessed August 21, 2025, [https://www.youtube.com/watch?v=iwRaNYa8yTw](https://www.youtube.com/watch?v=iwRaNYa8yTw)  
43. Assembling an entry level High Frequency Trading (HFT) system | by Gonçalo Abreu, accessed August 21, 2025, [https://medium.com/data-science/assembling-an-entry-level-high-frequency-trading-hft-system-e7538545b2a9](https://medium.com/data-science/assembling-an-entry-level-high-frequency-trading-hft-system-e7538545b2a9)  
44. Decoupling Architecture, accessed August 21, 2025, [https://www.itarch.info/2020/05/decoupling-architecture.html](https://www.itarch.info/2020/05/decoupling-architecture.html)  
45. Decoupled Architecture & Microservices | by Saurabh Gupta \- Medium, accessed August 21, 2025, [https://medium.com/@saurabh.engg.it/decoupled-architecture-microservices-29f7b201bd87](https://medium.com/@saurabh.engg.it/decoupled-architecture-microservices-29f7b201bd87)  
46. Mastering C++ Low Latency: A Guide to High-Frequency Trading Systems \- Quantlabs.net, accessed August 21, 2025, [https://www.quantlabsnet.com/post/mastering-c-low-latency-a-guide-to-high-frequency-trading-systems](https://www.quantlabsnet.com/post/mastering-c-low-latency-a-guide-to-high-frequency-trading-systems)  
47. C++ For High-Frequency Trading Systems \- HeyCoach | Blogs, accessed August 21, 2025, [https://heycoach.in/blog/c-for-high-frequency-trading-systems/](https://heycoach.in/blog/c-for-high-frequency-trading-systems/)  
48. Building a Market Data Feed with Liquibook | Object Computing, Inc., accessed August 21, 2025, [https://objectcomputing.com/resources/publications/sett/august-2013-building-a-market-data-feed-with-liquibook](https://objectcomputing.com/resources/publications/sett/august-2013-building-a-market-data-feed-with-liquibook)  
49. Designing a High Performance Market Data Feed Handler \- Erik Rigtorp, accessed August 21, 2025, [https://rigtorp.se/2012/11/22/feed-handler.html](https://rigtorp.se/2012/11/22/feed-handler.html)  
50. How to design high-frequency trading systems and its architecture. Part I, accessed August 21, 2025, [https://electronictradinghub.com/how-to-design-high-frequency-trading-systems-and-their-architecture-part-i/](https://electronictradinghub.com/how-to-design-high-frequency-trading-systems-and-their-architecture-part-i/)  
51. How to Setup a Trading Algorithm in C++ \- DayTrading.com, accessed August 21, 2025, [https://www.daytrading.com/setup-trading-algorithm-c](https://www.daytrading.com/setup-trading-algorithm-c)  
52. Implementing Trading Strategies in C++ \- AlgoDaily, accessed August 21, 2025, [https://algodaily.com/lessons/implementing-trading-strategies-in-c-e101dc7a/developing-trading-signals-68de0c21](https://algodaily.com/lessons/implementing-trading-strategies-in-c-e101dc7a/developing-trading-signals-68de0c21)  
53. How to Build an AI Quantitative Trading Bot from Scratch \- Biz4Group, accessed August 21, 2025, [https://www.biz4group.com/blog/build-ai-quantitative-trading-bot](https://www.biz4group.com/blog/build-ai-quantitative-trading-bot)  
54. Market Making Algorithms (Examples) \- QuestDB, accessed August 21, 2025, [https://questdb.com/glossary/market-making-algorithms/](https://questdb.com/glossary/market-making-algorithms/)  
55. Statistical Arbitrage Explained: A Complete Trading Guide \- TradeFundrr, accessed August 21, 2025, [https://tradefundrr.com/statistical-arbitrage-explained/](https://tradefundrr.com/statistical-arbitrage-explained/)  
56. What is Statistical Arbitrage? \- CQF, accessed August 21, 2025, [https://www.cqf.com/blog/quant-finance-101/what-is-statistical-arbitrage](https://www.cqf.com/blog/quant-finance-101/what-is-statistical-arbitrage)  
57. Introduction to ZeroMQ \- SE-EDU/LearningResources, accessed August 21, 2025, [https://se-education.org/learningresources/contents/zeromq/zeromq.html](https://se-education.org/learningresources/contents/zeromq/zeromq.html)  
58. ZeroMQ to MetaTrader Connectivity \- Darwinex, accessed August 21, 2025, [https://www.darwinex.com/algorithmic-trading/zeromq-metatrader](https://www.darwinex.com/algorithmic-trading/zeromq-metatrader)  
59. Publish/Subscribe — Learning 0MQ with examples, accessed August 21, 2025, [https://learning-0mq-with-pyzmq.readthedocs.io/en/latest/pyzmq/patterns/pubsub.html](https://learning-0mq-with-pyzmq.readthedocs.io/en/latest/pyzmq/patterns/pubsub.html)  
60. azmisaquib/zmq-pubsub: ZeroMQ C++ Publish-Subscribe Example \- GitHub, accessed August 21, 2025, [https://github.com/azmisaquib/zmq-pubsub](https://github.com/azmisaquib/zmq-pubsub)  
61. 1\. Basics | ØMQ \- The Guide, accessed August 21, 2025, [https://zguide.zeromq.org/docs/chapter1/](https://zguide.zeromq.org/docs/chapter1/)  
62. Chapter 5 \- Advanced Pub-Sub Patterns \- ZeroMQ Guide, accessed August 21, 2025, [https://zguide.zeromq.org/docs/chapter5/](https://zguide.zeromq.org/docs/chapter5/)  
63. ZeroMQ inter process communication from single thread loses messages \- Stack Overflow, accessed August 21, 2025, [https://stackoverflow.com/questions/15945915/zeromq-inter-process-communication-from-single-thread-loses-messages](https://stackoverflow.com/questions/15945915/zeromq-inter-process-communication-from-single-thread-loses-messages)  
64. Understanding Inter-Process Communications in C++ using ZeroMQ \- Wiyogo Tech, accessed August 21, 2025, [https://wiyogo.com/blog/20230114/understanding-inter-process-communications-in-c-using-zero-mq/](https://wiyogo.com/blog/20230114/understanding-inter-process-communications-in-c-using-zero-mq/)  
65. Chapter 2 \- Sockets and Patterns \- ZeroMQ Guide, accessed August 21, 2025, [https://zguide.zeromq.org/docs/chapter2/](https://zguide.zeromq.org/docs/chapter2/)  
66. Writing Low-Latency C++ Applications \- Medium, accessed August 21, 2025, [https://medium.com/@AlexanderObregon/writing-low-latency-c-applications-f759c94f52f8](https://medium.com/@AlexanderObregon/writing-low-latency-c-applications-f759c94f52f8)  
67. How do they use C++ in HFT(High Frequency Trade ) industry? : r/cpp \- Reddit, accessed August 21, 2025, [https://www.reddit.com/r/cpp/comments/1d8ukso/how\_do\_they\_use\_c\_in\_hfthigh\_frequency\_trade/](https://www.reddit.com/r/cpp/comments/1d8ukso/how_do_they_use_c_in_hfthigh_frequency_trade/)  
68. What is the "Land of Despair" for high frequency trading engineers in C++?, accessed August 21, 2025, [https://www.efinancialcareers.com/news/what-is-the-land-of-despair-for-high-frequency-trading-engineers](https://www.efinancialcareers.com/news/what-is-the-land-of-despair-for-high-frequency-trading-engineers)  
69. Trading at light speed: designing low latency systems in C++ \- David Gross \- YouTube, accessed August 21, 2025, [https://www.youtube.com/watch?v=8uAW5FQtcvE](https://www.youtube.com/watch?v=8uAW5FQtcvE)  
70. C++ Design Patterns for Low Latency Applications Including High Frequency Trading | @ieg, accessed August 21, 2025, [https://programmador.com/series/notes/cpp-design-patterns-for-low-latency-apps/](https://programmador.com/series/notes/cpp-design-patterns-for-low-latency-apps/)  
71. How do I design high-frequency trading systems and its architecture. Part II, accessed August 21, 2025, [https://electronictradinghub.com/how-do-i-design-high-frequency-trading-systems-and-its-architecture-part-ii/](https://electronictradinghub.com/how-do-i-design-high-frequency-trading-systems-and-its-architecture-part-ii/)  
72. C++ design patterns for low-latency applications including high-frequency trading \- arXiv, accessed August 21, 2025, [https://arxiv.org/pdf/2309.04259](https://arxiv.org/pdf/2309.04259)  
73. Alpaca Trading Review \- Pros and Cons \- AlgoTrading101 Blog, accessed August 21, 2025, [https://algotrading101.com/learn/alpaca-trading-review/](https://algotrading101.com/learn/alpaca-trading-review/)  
74. Alpaca Launches Data API, accessed August 21, 2025, [https://alpaca.markets/blog/market-data-apiv2/](https://alpaca.markets/blog/market-data-apiv2/)  
75. Real-time Stock Data \- Alpaca API Docs, accessed August 21, 2025, [https://docs.alpaca.markets/docs/real-time-stock-pricing-data](https://docs.alpaca.markets/docs/real-time-stock-pricing-data)  
76. REST vs WebSocket Crypto: API Comparison for Bots in 2025 \- Token Metrics, accessed August 21, 2025, [https://www.tokenmetrics.com/blog/crypto-api-bot-rest-vs-websockets](https://www.tokenmetrics.com/blog/crypto-api-bot-rest-vs-websockets)  
77. Why WebSocket Multiple Updates Beat REST APIs for Real-Time Crypto Trading, accessed August 21, 2025, [https://www.coinapi.io/blog/why-websocket-multiple-updates-beat-rest-apis-for-real-time-crypto-trading](https://www.coinapi.io/blog/why-websocket-multiple-updates-beat-rest-apis-for-real-time-crypto-trading)  
78. Polygon.io \- Stock Market API, accessed August 21, 2025, [https://polygon.io/](https://polygon.io/)  
79. Alpaca Trading API Guide \- A Step-by-step Guide \- AlgoTrading101 Blog, accessed August 21, 2025, [https://algotrading101.com/learn/alpaca-trading-api-guide/](https://algotrading101.com/learn/alpaca-trading-api-guide/)  
80. Finnhub Stock APIs \- Real-time stock prices, Company fundamentals, Estimates, and Alternative data., accessed August 21, 2025, [https://finnhub.io/](https://finnhub.io/)  
81. Free Stock Market API and Financial Statements API... | FMP, accessed August 21, 2025, [https://site.financialmodelingprep.com/developer/docs](https://site.financialmodelingprep.com/developer/docs)  
82. Algorithmic Trading API, Commission-Free \- Alpaca, accessed August 21, 2025, [https://alpaca.markets/algotrading](https://alpaca.markets/algotrading)  
83. Alpaca Trading Review 2025: Fees, Services and More \- SmartAsset, accessed August 21, 2025, [https://smartasset.com/investing/alpaca-trading](https://smartasset.com/investing/alpaca-trading)  
84. IBKR vs Alpaca? : r/algotrading \- Reddit, accessed August 21, 2025, [https://www.reddit.com/r/algotrading/comments/wzspd2/ibkr\_vs\_alpaca/](https://www.reddit.com/r/algotrading/comments/wzspd2/ibkr_vs_alpaca/)  
85. Step by Step Tutorial Videos \- Documentation \- Alpaca, accessed August 21, 2025, [https://alpaca.markets/deprecated/docs/get-started-with-alpaca/tutorial-videos/](https://alpaca.markets/deprecated/docs/get-started-with-alpaca/tutorial-videos/)  
86. Paper Trading \- Alpaca API Docs, accessed August 21, 2025, [https://docs.alpaca.markets/docs/paper-trading](https://docs.alpaca.markets/docs/paper-trading)  
87. How to Start Paper Trading with Alpaca's Trading API, accessed August 21, 2025, [https://alpaca.markets/learn/start-paper-trading](https://alpaca.markets/learn/start-paper-trading)  
88. IBKR Trading API Solutions | Interactive Brokers LLC, accessed August 21, 2025, [https://www.interactivebrokers.com/en/trading/ib-api.php](https://www.interactivebrokers.com/en/trading/ib-api.php)  
89. Interactive Brokers Review \- Investopedia, accessed August 21, 2025, [https://www.investopedia.com/interactive-brokers-review-4587904](https://www.investopedia.com/interactive-brokers-review-4587904)  
90. Compare Alpaca Trading vs Interactive Brokers for fees, safety and more \- BrokerChooser, accessed August 21, 2025, [https://brokerchooser.com/compare/alpaca-trading-vs-interactive-brokers](https://brokerchooser.com/compare/alpaca-trading-vs-interactive-brokers)  
91. Should I use Alpaca or IBKR for API trading? : r/algotrading \- Reddit, accessed August 21, 2025, [https://www.reddit.com/r/algotrading/comments/o9tb21/should\_i\_use\_alpaca\_or\_ibkr\_for\_api\_trading/](https://www.reddit.com/r/algotrading/comments/o9tb21/should_i_use_alpaca_or_ibkr_for_api_trading/)  
92. C++ API Quick Reference \- Interactive Brokers, accessed August 21, 2025, [https://www.interactivebrokers.com/download/C++APIQuickReference.pdf](https://www.interactivebrokers.com/download/C++APIQuickReference.pdf)  
93. TWS API v9.72+: Introduction \- Interactive Brokers \- API Software, accessed August 21, 2025, [https://interactivebrokers.github.io/tws-api/introduction.html](https://interactivebrokers.github.io/tws-api/introduction.html)  
94. Interactive Brokers C++ POS API example? \- Stack Overflow, accessed August 21, 2025, [https://stackoverflow.com/questions/10805323/interactive-brokers-c-pos-api-example](https://stackoverflow.com/questions/10805323/interactive-brokers-c-pos-api-example)  
95. Statistical Arbitrage: Definition, How It Works, and Example \- Investopedia, accessed August 21, 2025, [https://www.investopedia.com/terms/s/statisticalarbitrage.asp](https://www.investopedia.com/terms/s/statisticalarbitrage.asp)  
96. Top Statistical Arbitrage Strategies Explained \- WunderTrading, accessed August 21, 2025, [https://wundertrading.com/journal/en/learn/article/statistical-arbitrage-strategies](https://wundertrading.com/journal/en/learn/article/statistical-arbitrage-strategies)  
97. Statistical Arbitrage \- CFA, FRM, and Actuarial Exams Study Notes \- AnalystPrep, accessed August 21, 2025, [https://analystprep.com/study-notes/cfa-level-iii/statistical-arbitrage/](https://analystprep.com/study-notes/cfa-level-iii/statistical-arbitrage/)  
98. 15.4 Discovering Cointegrated Pairs | Portfolio Optimization \- Bookdown, accessed August 21, 2025, [https://bookdown.org/palomar/portfoliooptimizationbook/15.4-discovering-pairs.html](https://bookdown.org/palomar/portfoliooptimizationbook/15.4-discovering-pairs.html)  
99. Pairs Trading for Beginners: Correlation, Cointegration, Examples, and Strategy Steps, accessed August 21, 2025, [https://blog.quantinsti.com/pairs-trading-basics/](https://blog.quantinsti.com/pairs-trading-basics/)  
100. bookdown.org, accessed August 21, 2025, [https://bookdown.org/palomar/portfoliooptimizationbook/15.4-discovering-pairs.html\#:\~:text=One%20of%20the%20simplest%20and,residual%20is%20tested%20for%20stationarity.](https://bookdown.org/palomar/portfoliooptimizationbook/15.4-discovering-pairs.html#:~:text=One%20of%20the%20simplest%20and,residual%20is%20tested%20for%20stationarity.)  
101. Pairs trading using cointegration approach \- University of Wollongong Research Online, accessed August 21, 2025, [https://ro.uow.edu.au/articles/thesis/Pairs\_trading\_using\_cointegration\_approach/27662274](https://ro.uow.edu.au/articles/thesis/Pairs_trading_using_cointegration_approach/27662274)  
102. Pairs trading based on cointegration \- Databento, accessed August 21, 2025, [https://databento.com/docs/examples/algo-trading/pairs-trading](https://databento.com/docs/examples/algo-trading/pairs-trading)  
103. Cointegration: The Engle and Granger approach \- University of Warwick, accessed August 21, 2025, [https://warwick.ac.uk/fac/soc/economics/staff/gboero/personal/hand2\_cointeg.pdf](https://warwick.ac.uk/fac/soc/economics/staff/gboero/personal/hand2_cointeg.pdf)  
104. Test for Cointegration Using the Engle-Granger Test \- MATLAB & Simulink \- MathWorks, accessed August 21, 2025, [https://www.mathworks.com/help/econ/test-for-cointegration-using-the-engle-granger-test.html](https://www.mathworks.com/help/econ/test-for-cointegration-using-the-engle-granger-test.html)  
105. Tests for Cointegration — arbitragelab 1.0.0 documentation \- Read the Docs, accessed August 21, 2025, [https://hudson-and-thames-arbitragelab.readthedocs-hosted.com/en/latest/cointegration\_approach/cointegration\_tests.html](https://hudson-and-thames-arbitragelab.readthedocs-hosted.com/en/latest/cointegration_approach/cointegration_tests.html)  
106. Pairs Trading Strategy With Logic And Rules \- QuantifiedStrategies.com, accessed August 21, 2025, [https://www.quantifiedstrategies.com/pairs-trading-strategy/](https://www.quantifiedstrategies.com/pairs-trading-strategy/)  
107. Crypto Pairs Trading: Part 3 — Constructing Your Strategy with Logs, Hedge Ratios, and Z-Scores \- Amberdata Blog, accessed August 21, 2025, [https://blog.amberdata.io/constructing-your-strategy-with-logs-hedge-ratios-and-z-scores](https://blog.amberdata.io/constructing-your-strategy-with-logs-hedge-ratios-and-z-scores)  
108. In Pairs Trading, After finding good pairs, how exactly do I implement them on the trading period? : r/quant \- Reddit, accessed August 21, 2025, [https://www.reddit.com/r/quant/comments/1l6i3p7/in\_pairs\_trading\_after\_finding\_good\_pairs\_how/](https://www.reddit.com/r/quant/comments/1l6i3p7/in_pairs_trading_after_finding_good_pairs_how/)  
109. Backtesting An Intraday Mean Reversion Pairs Strategy Between SPY And IWM | QuantStart, accessed August 21, 2025, [https://www.quantstart.com/articles/Backtesting-An-Intraday-Mean-Reversion-Pairs-Strategy-Between-SPY-And-IWM/](https://www.quantstart.com/articles/Backtesting-An-Intraday-Mean-Reversion-Pairs-Strategy-Between-SPY-And-IWM/)  
110. What is your exit strategy in pairs trading? Is it half life of mean reversion? equity-based percentage stop loss? \- Reddit, accessed August 21, 2025, [https://www.reddit.com/r/quant/comments/199owk5/what\_is\_your\_exit\_strategy\_in\_pairs\_trading\_is\_it/](https://www.reddit.com/r/quant/comments/199owk5/what_is_your_exit_strategy_in_pairs_trading_is_it/)  
111. Statistical Arbitrage Strategies \- Number Analytics, accessed August 21, 2025, [https://www.numberanalytics.com/blog/statistical-arbitrage-guide](https://www.numberanalytics.com/blog/statistical-arbitrage-guide)  
112. Mastering Statistical Arbitrage in Trading \- Number Analytics, accessed August 21, 2025, [https://www.numberanalytics.com/blog/statistical-arbitrage-trading-guide](https://www.numberanalytics.com/blog/statistical-arbitrage-trading-guide)  
113. Pairs trade \- Wikipedia, accessed August 21, 2025, [https://en.wikipedia.org/wiki/Pairs\_trade](https://en.wikipedia.org/wiki/Pairs_trade)  
114. Pairs Trading: A Deep Dive into this Market Neutral Strategy \- Bookmap, accessed August 21, 2025, [https://bookmap.com/blog/pairs-trading-a-deep-dive-into-this-market-neutral-strategy](https://bookmap.com/blog/pairs-trading-a-deep-dive-into-this-market-neutral-strategy)  
115. Top 10 Best Front End Frameworks in 2025 Compared \- Imaginary Cloud, accessed August 21, 2025, [https://www.imaginarycloud.com/blog/best-frontend-frameworks](https://www.imaginarycloud.com/blog/best-frontend-frameworks)  
116. Vue vs React: Which is Best for Frontend Development in 2025? \- MindInventory, accessed August 21, 2025, [https://www.mindinventory.com/blog/reactjs-vs-vuejs/](https://www.mindinventory.com/blog/reactjs-vs-vuejs/)  
117. React Dashboard Libraries: Which One To Use in 2025? \- Luzmo, accessed August 21, 2025, [https://www.luzmo.com/blog/react-dashboard](https://www.luzmo.com/blog/react-dashboard)  
118. 100+ React Dashboard Components to Use in 2025 \- DEV Community, accessed August 21, 2025, [https://dev.to/tailwindcss/100-react-dashboard-components-to-use-in-2024-3ked](https://dev.to/tailwindcss/100-react-dashboard-components-to-use-in-2024-3ked)  
119. What React dashboards have you had success with? : r/reactjs \- Reddit, accessed August 21, 2025, [https://www.reddit.com/r/reactjs/comments/h0cgt0/what\_react\_dashboards\_have\_you\_had\_success\_with/](https://www.reddit.com/r/reactjs/comments/h0cgt0/what_react_dashboards_have_you_had_success_with/)  
120. WebSocket vs REST: Key differences and which to use \- Ably, accessed August 21, 2025, [https://ably.com/topic/websocket-vs-rest](https://ably.com/topic/websocket-vs-rest)  
121. WebSocket C++: The Definitive 2025 Guide for Real-Time Communication \- VideoSDK, accessed August 21, 2025, [https://www.videosdk.live/developer-hub/websocket/websocket-c](https://www.videosdk.live/developer-hub/websocket/websocket-c)  
122. WebSocket \- Wikipedia, accessed August 21, 2025, [https://en.wikipedia.org/wiki/WebSocket](https://en.wikipedia.org/wiki/WebSocket)  
123. Drogon Web Framework: Homepage, accessed August 21, 2025, [https://drogon.org/](https://drogon.org/)  
124. ENG 01 Overview · drogonframework/drogon Wiki \- GitHub, accessed August 21, 2025, [https://github.com/drogonframework/drogon/wiki/ENG-01-Overview](https://github.com/drogonframework/drogon/wiki/ENG-01-Overview)  
125. Building RESTful APIs with C++ \- Medium, accessed August 21, 2025, [https://medium.com/@AlexanderObregon/building-restful-apis-with-c-4c8ac63fe8a7](https://medium.com/@AlexanderObregon/building-restful-apis-with-c-4c8ac63fe8a7)  
126. Effective Dashboard UX: Design Principles & Best Practices \- Excited agency, accessed August 21, 2025, [https://www.excited.agency/blog/effective-dashboard-ux-design-principles-best-practices](https://www.excited.agency/blog/effective-dashboard-ux-design-principles-best-practices)  
127. Dashboard Design: 7 Best Practices & Examples \- Qlik, accessed August 21, 2025, [https://www.qlik.com/us/dashboard-examples/dashboard-design](https://www.qlik.com/us/dashboard-examples/dashboard-design)  
128. Dashboard Design: best practices and examples \- Justinmind, accessed August 21, 2025, [https://www.justinmind.com/ui-design/dashboard-design-best-practices-ux](https://www.justinmind.com/ui-design/dashboard-design-best-practices-ux)  
129. Dashboard Design UX Patterns Best Practices \- Pencil & Paper, accessed August 21, 2025, [https://www.pencilandpaper.io/articles/ux-pattern-analysis-data-dashboards](https://www.pencilandpaper.io/articles/ux-pattern-analysis-data-dashboards)  
130. How to Deploy C++ Applications on Kubernetes Effectively \- Devtron, accessed August 21, 2025, [https://devtron.ai/blog/how-to-deploy-cpp-applications-on-kubernetes-effectively/](https://devtron.ai/blog/how-to-deploy-cpp-applications-on-kubernetes-effectively/)  
131. Kernel Bypass Techniques in Linux for High-Frequency Trading: A Deep Dive | by Yogesh, accessed August 22, 2025, [https://lambdafunc.medium.com/kernel-bypass-techniques-in-linux-for-high-frequency-trading-a-deep-dive-de347ccd5407](https://lambdafunc.medium.com/kernel-bypass-techniques-in-linux-for-high-frequency-trading-a-deep-dive-de347ccd5407)  
132. Using kernel bypass \- Packt+ | Advance your knowledge in tech, accessed August 22, 2025, [https://www.packtpub.com/en-us/product/developing-high-frequency-trading-systems-9781803242811/chapter/chapter-7-hft-optimization-logging-performance-and-networking-9/section/using-kernel-bypass-ch09lvl1sec44](https://www.packtpub.com/en-us/product/developing-high-frequency-trading-systems-9781803242811/chapter/chapter-7-hft-optimization-logging-performance-and-networking-9/section/using-kernel-bypass-ch09lvl1sec44)  
133. The High-Frequency Trading Developer's Guide: Six Key Components for Low Latency and Scalability | HackerNoon, accessed August 22, 2025, [https://hackernoon.com/the-high-frequency-trading-developers-guide-six-key-components-for-low-latency-and-scalability](https://hackernoon.com/the-high-frequency-trading-developers-guide-six-key-components-for-low-latency-and-scalability)  
134. Zero-copy network transmission with io\_uring \- LWN.net, accessed August 22, 2025, [https://lwn.net/Articles/879724/](https://lwn.net/Articles/879724/)  
135. A Deep Dive into Zero-Copy Networking and io\_uring | by Jatin mamtora \- Medium, accessed August 22, 2025, [https://medium.com/@jatinumamtora/a-deep-dive-into-zero-copy-networking-and-io-uring-78914aa24029](https://medium.com/@jatinumamtora/a-deep-dive-into-zero-copy-networking-and-io-uring-78914aa24029)  
136. Unleashing I/O Performance with io\_uring: A Deep Dive | by Alpesh Dhamelia | Medium, accessed August 22, 2025, [https://medium.com/@alpesh.ccet/unleashing-i-o-performance-with-io-uring-a-deep-dive-54924e64791f](https://medium.com/@alpesh.ccet/unleashing-i-o-performance-with-io-uring-a-deep-dive-54924e64791f)  
137. Bare Metal vs Cloud in 2025: Cost, Performance, and When to Choose Each \- Unihost, accessed August 22, 2025, [https://unihost.com/blog/bare-metal-vs-cloud-2025/](https://unihost.com/blog/bare-metal-vs-cloud-2025/)  
138. Bare Metal vs Cloud Servers:The Right Infrastructure Matters, accessed August 22, 2025, [https://www.servers.com/news/blog/bare-metal-vs-cloud-servers-what-s-the-difference](https://www.servers.com/news/blog/bare-metal-vs-cloud-servers-what-s-the-difference)  
139. Boost Your Network Performance With DPDK \- Medium, accessed August 22, 2025, [https://medium.com/@vayavyalabs.com/boost-your-network-performance-with-dpdk-66778dc77d1b](https://medium.com/@vayavyalabs.com/boost-your-network-performance-with-dpdk-66778dc77d1b)  
140. YAStack: User-space network-stack based on DPDK, FreeBSD TCP/IP Stack, EnvoyProxy \- GitHub, accessed August 22, 2025, [https://github.com/saaras-io/yastack](https://github.com/saaras-io/yastack)  
141. User Space TCP \- Getting LKL Ready for the Prime Time \- NetDev conference, accessed August 22, 2025, [https://www.netdevconf.info/1.2/papers/jerry\_chu.pdf](https://www.netdevconf.info/1.2/papers/jerry_chu.pdf)  
142. Why you should use io\_uring for network I/O | Red Hat Developer, accessed August 22, 2025, [https://developers.redhat.com/articles/2023/04/12/why-you-should-use-iouring-network-io](https://developers.redhat.com/articles/2023/04/12/why-you-should-use-iouring-network-io)  
143. Zero-Copy I/O: From sendfile to io\_uring – Evolution and Impact on Latency in Distributed Logs \- Codemia, accessed August 22, 2025, [https://codemia.io/blog/path/Zero-Copy-IO-From-sendfile-to-iouring--Evolution-and-Impact-on-Latency-in-Distributed-Logs](https://codemia.io/blog/path/Zero-Copy-IO-From-sendfile-to-iouring--Evolution-and-Impact-on-Latency-in-Distributed-Logs)  
144. What Is Backtesting & How to Backtest a Trading Strategy Using Python \- QuantInsti, accessed August 21, 2025, [https://www.quantinsti.com/articles/backtesting-trading/](https://www.quantinsti.com/articles/backtesting-trading/)  
145. How to Backtest High-Frequency Trading Strategies Effectively, accessed August 21, 2025, [https://blog.afterpullback.com/backtesting-high-frequency-trading-strategies-a-practical-guide/](https://blog.afterpullback.com/backtesting-high-frequency-trading-strategies-a-practical-guide/)  
146. Exploring Quantitative Trading: Challenges, Benefits & Future Prospects \- Equirus Wealth, accessed August 21, 2025, [https://www.equiruswealth.com/blog/exploring-quantitative-trading-challenges-benefits-and-future-prospects](https://www.equiruswealth.com/blog/exploring-quantitative-trading-challenges-benefits-and-future-prospects)  
147. Backtesting Market Making Strategy or Microstructure Strategy \- Quantitative Finance Stack Exchange, accessed August 21, 2025, [https://quant.stackexchange.com/questions/38781/backtesting-market-making-strategy-or-microstructure-strategy](https://quant.stackexchange.com/questions/38781/backtesting-market-making-strategy-or-microstructure-strategy)  
148. Awesome Systematic Trading \- FunCoder, accessed August 22, 2025, [https://wangzhe3224.github.io/awesome-systematic-trading/](https://wangzhe3224.github.io/awesome-systematic-trading/)  
149. nkaz001/hftbacktest: Free, open source, a high frequency trading and market making backtesting and trading bot, which accounts for limit orders, queue positions, and latencies, utilizing full tick data for trades and order books(Level-2 and Level-3), with real-world crypto trading examples for Binance \- GitHub, accessed August 22, 2025, [https://github.com/nkaz001/hftbacktest](https://github.com/nkaz001/hftbacktest)  
150. High-Frequency Trading Backtesting Tool \- HftBacktest, accessed August 22, 2025, [https://hftbacktest.readthedocs.io/en/v1.8.4/](https://hftbacktest.readthedocs.io/en/v1.8.4/)  
151. (PDF) High-Frequency Trading \- ResearchGate, accessed August 21, 2025, [https://www.researchgate.net/publication/228261374\_High-Frequency\_Trading](https://www.researchgate.net/publication/228261374_High-Frequency_Trading)  
152. Top 11 bare metal cloud hosting providers for 2025 \- Liquid Web, accessed August 22, 2025, [https://www.liquidweb.com/blog/bare-metal-cloud-providers/](https://www.liquidweb.com/blog/bare-metal-cloud-providers/)  
153. Bare Metal vs. VPS Hosting: An In-Depth Comparison \- Brightlio, accessed August 22, 2025, [https://brightlio.com/bare-metal-vs-vps-hosting/](https://brightlio.com/bare-metal-vs-vps-hosting/)  
154. Low Latency Dedicated Trading Servers for Companies \- Cherry Servers, accessed August 22, 2025, [https://www.cherryservers.com/trading-servers](https://www.cherryservers.com/trading-servers)  
155. Trading Dedicated Server in Chicago \- Rental \- MangoHost, accessed August 22, 2025, [https://mangohost.net/dedicated/server-trading-chicago](https://mangohost.net/dedicated/server-trading-chicago)  
156. QuantVPS: The Best VPS for Futures Trading, accessed August 22, 2025, [https://www.quantvps.com/](https://www.quantvps.com/)  
157. Speedy Trading Servers | Low latency hosting for algorithmic trading, accessed August 22, 2025, [https://www.speedytradingservers.com/](https://www.speedytradingservers.com/)  
158. Bare Metal Dedicated Servers \- OVHcloud, accessed August 22, 2025, [https://us.ovhcloud.com/bare-metal/](https://us.ovhcloud.com/bare-metal/)  
159. Dedicated Servers | Enterprise Bare Metal Hosting Solutions \- HostVenom, accessed August 22, 2025, [https://hostvenom.com/services/dedicated/](https://hostvenom.com/services/dedicated/)  
160. Optimizing Costs in the Cloud: VMs vs. VPS vs. Bare Metal | Atlantic.Net, accessed August 22, 2025, [https://www.atlantic.net/dedicated-server-hosting/optimizing-costs-in-the-cloud-vms-vs-vps-vs-bare-metal/](https://www.atlantic.net/dedicated-server-hosting/optimizing-costs-in-the-cloud-vms-vs-vps-vs-bare-metal/)

<Some Unimplemented Idea on demand>
* Core Affinity
* Atomic race condition?
* Prometheus and Grafana, and the ability to show this dashboard in frontend 
* Connecting to dashboard
** Brainstorm functionality and control API we should have for the dashbaord 
** Define interface between them
* For metrics to show
** live trading event
** latency metrics per compoenet
** How do they interact? Poll or event based?