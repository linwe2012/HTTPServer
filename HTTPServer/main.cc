#pragma once
#define _CRT_SECURE_NO_WARNINGS
#include "server.h"

#define SERVER_PORT	80

#include <string>
#include <map>
#include <chrono>

class HttpServer {
public:
	using Callback = std::function<void(std::shared_ptr<Request>, std::shared_ptr<Response>)>;
private:
	Server server_;
	std::map<std::string, Callback> gets_;
	std::map<std::string, Callback> posts_;
public:
	HttpServer() {
		server_.CLIHogging(false);
		server_.Register([this](std::shared_ptr<Request> preq, std::shared_ptr<Response> pres) {
			HandleUrl(preq, pres);
		});
	}

	void HandleUrl(std::shared_ptr<Request> preq, std::shared_ptr<Response> pres) {
		auto& req = *preq;
		auto& res = *pres;

		if (req.method == Request::kGet) {
			auto itr = gets_.find(req.url);
			if (itr != gets_.end()) {
				std::cout << ":: GET " << req.url << std::endl;
				return itr->second(preq, pres);
			}
		}
		else if (req.method == Request::kPost) {
			auto itr = posts_.find(req.url);
			if (itr != posts_.end()) {
				std::cout << ":: POST " << req.url << std::endl;
				return itr->second(preq, pres);
			}
		}
		res.ReturnCode(404).End();
	}

	int Listen(int port) {
		return server_.Listen(port);
	}

	void Get(std::string url, Callback cb) {
		gets_[url] = cb;
	}

	void Post(std::string url, Callback cb) {
		posts_[url] = cb;
	}
};

int main()
{
	HttpServer app;
	app.Get("/", [](std::shared_ptr<Request> preq, std::shared_ptr<Response> pres) {
		auto& req = *preq;
		auto& res = *pres;
		
		res.SendFile("index.html");

	});

	app.Get("/img/logo.png", [](std::shared_ptr<Request> preq, std::shared_ptr<Response> pres) {
		auto& req = *preq;
		auto& res = *pres;

		res.SendFile("img/logo.png");

	});

	app.Post("/dopost", [](std::shared_ptr<Request> preq, std::shared_ptr<Response> pres) {
		auto& req = *preq;
		auto& res = *pres;

		auto form = req.ParseForm();

		if (form["login"] == "3170105728" && form["pass"] == "5728") {
			res.End("<html><body>Login Success</body></html>");
		}
		else {
			res.End("<html><body>Login Failed</body></html>");
		}
	});

	app.Listen(SERVER_PORT);
}