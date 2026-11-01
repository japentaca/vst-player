#pragma once

// Network side of the local API (stage 7): HTTP + WebSocket on one loopback port, built on
// cpp-httplib. Knows nothing about JUCE or the engine: every request body / WebSocket text
// message is handed to the RequestHandler (on a network thread) and its answer is sent back.
// Kept in its own translation unit so the Winsock headers of cpp-httplib never meet JUCE's.
//
// Routes:
//   POST /api              body = JSON command, answer = JSON
//   GET  /api/status       same as POST /api {"cmd":"status"}
//   GET  /                 small JSON with the name and the version of the API
//   GET  /client.js        examples/carrilion-client.js (embedded at build time), for import()
//   OPTIONS *              CORS preflight
//   WS   /  and  /ws       one JSON command per text message; answers and pushed events
// Every HTTP answer carries Access-Control-Allow-Origin: * (and Allow-Private-Network for
// Chrome's private network access preflight).
// Token (optional): when set, every request (except /client.js) needs ?token=<token> (or the X-Api-Token header);
// otherwise HTTP 401 / the WebSocket gets an error and is closed with code 1008.

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace vstplayer::api
{

class ApiServer final
{
public:
    struct Response
    {
        int status = 200;
        std::string body; // JSON
    };

    // Called on a network thread (several may run at once). `body` is UTF-8.
    using RequestHandler = std::function<Response (const std::string& body)>;

    ApiServer();
    ~ApiServer();

    // Binds host:port and starts the listening thread. False + error if the port is taken.
    bool start (const std::string& host, int port, const std::string& token, RequestHandler handler, std::string& error);
    // Closes the WebSockets, stops listening and joins the threads. Safe to call twice.
    void stop();

    bool isRunning() const noexcept;
    int getNumClients() const noexcept;        // open WebSockets
    long long getNumRequests() const noexcept; // HTTP requests + WebSocket messages handled

    // Sends a text message to every open WebSocket (any thread).
    void broadcast (const std::string& text);

    // Called (on a network thread) whenever a WebSocket opens or closes.
    std::function<void()> onClientsChanged;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace vstplayer::api
