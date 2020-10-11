#pragma once
#include "common.h"
#include "thread-pool.h"
#include <iostream>
#include <map>
#include <future>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <filesystem>

// Link with ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

#else
#include <sys/socket>
#include <sys/types.h>
#include <unistd.h>
#endif // _WIN32

class Response;
class Request;


class Server {
public:
	using Handler = std::function<void(std::shared_ptr<Request>, std::shared_ptr<Response>)>;
	
	struct ServerProxy {
		ServerProxy(Server* server, int connect_fd_, sockaddr_in* client_addr)
			:server_(server), connect_fd_(connect_fd_), client_addr_(client_addr)
		{}
		
		void SendSync(uint32_t ip, int port, char* buf, uint32_t size);

		void ReplySync(char* buffer, uint32_t size) {
			server_->ReplySync(connect_fd_, buffer, size);
		}

		~ServerProxy();

		const std::map<uint64_t, sockaddr_in>& GetConnectionInfo();

		void Relay(int fd, const char* buf, uint32_t size);

		void CloseConnection();

		void EnableCleanup() { cleanup_ = true; }
		void DisableCleanup() { cleanup_ = false; }
		int Getfd() { return connect_fd_; }

		void ReportStatus(std::string s);

	private:

		friend class Response;
		friend class Request;
		Server* server_;
		int connect_fd_;
		sockaddr_in* client_addr_;
		bool cleanup_ = false;
	};

	void Register(Handler handler) { handler_ = handler; };

	int Listen(int port);

	Server& CLIHogging(bool what) {
		cli_hogging_ = what;
		return *this;
	}

	void Shutdown();

private:
	friend class Response;
	friend class Request;

	void ProcessRequest(int connect_fd, sockaddr_in* client_addr);

	void SendSync(uint32_t ip, int port, char* buffer, uint32_t size);

	void ReplySync(int fd, const char* buffer, uint32_t size);

	void Error(std::string s) {
		std::cerr << s;
	}
	void Info(std::string s) {
		std::cerr << s;
	}

	void CloseConnection(int connect_fd);

	void CLIMoniter();

	Handler handler_;
	ThreadPool pool_;
	
	struct addrinfo server_info;
	struct sockaddr_in server_sock;
	std::function<void(int /*connect fd*/, std::string /*msg*/, bool/*is closing*/)> report_status_;
	std::function<void(int /*connect fd*/, std::string, std::string)> set_tag_;
	int sockfd = -1;
	int sendfd = -1;
	int protocol_version_ = 0;
	uint32_t max_buf_ = 1024 * 1024 * 4; // 4MB
	std::mutex clients_mutex_;
	std::map<uint64_t, sockaddr_in> clients_;
	int port_;
	bool cli_hogging_ = true;
	bool should_close_ = false;
};

std::string GetIPString(const sockaddr_in& addr);

class Request {
public:
	enum Method {
		kInvalid,
		kGet,
		kPost
	};

	Request(std::shared_ptr<Server::ServerProxy> _server,
		    char* buffer, 
		    uint32_t buffer_size)
		: server_(_server),
		  server(*_server),
		  stream(buffer, buffer_size)
	{
		auto the_method = stream.StringEndBySpace();

		if (the_method == std::string("GET")) {
			method = kGet;
		}
		else if (the_method == std::string("POST")) {
			method = kPost;
		}
		else {
			method = kInvalid;
			return;
		}

		url = stream.StringEndBySpace();
		protocol_version = stream.StringEndByCRLF();

		while (!stream.IsCRLF())
		{
			auto key = stream.StringEndBy(':');
			if (key == nullptr) {
				return;
			}
			auto val = stream.StringEndByCRLF();
			if (val == nullptr) {
				return;
			}
			headers[key] = val;
		}
		stream.SkipBytes(2); // eat '\r\n'
		
		body = stream.current;
		*stream.end = '\n';
	}

	Method method;
	char* url = nullptr;
	char* protocol_version = nullptr;
	std::map<std::string, std::string> headers;
	char* body = nullptr;

	BufferStream stream;//文件流
	Server::ServerProxy& server;//服务器代理
	std::map<std::string, std::string> ParseForm();
private:
	std::shared_ptr<Server::ServerProxy> server_;
};


class Response {
public:
	Response(std::shared_ptr<Server::ServerProxy> _server,
		     char* buffer, uint32_t buffer_size);

	Response& ReturnCode(int return_code) {
		return_code_ = return_code;
		return *this;
	}
	Response& SendFile(std::string path);

	std::string GetTimeString();

	template<typename T>
	Response& Write(const T& what) {
		WriteDelegate<T>::Write(what, buf_);
		return *this;
	}

	

	Response& WriteRaw(const char* beg, const char* end) {
		buf_.insert(buf_.begin(), beg, end);
	}

	template<typename T>
	Response& Reserve() {
		int tmp = static_cast<int>(buf_.size());
		Write<T>(T());
		reserve_pos_ = tmp;
		reserved_size_ = sizeof(T);
		return *this;
	}

	template<typename T>
	Response& WriteReserve(T& data) {
		if (reserve_pos_ == -1) {
			throw std::exception("Write Response: No reservation");
		}

		if (sizeof(T) != reserved_size_) {
			throw std::exception("Write Response: wrong reservation size");
		}
		memcpy(&buf_.front() + reserve_pos_, &data, sizeof(T));
		reserve_pos_ = -1;///我觉得这里有问题
		return *this;
	}

	void End(std::string what = "");

	Server::ServerProxy& server;

	template<typename T>
	struct WriteDelegate {
		static void Write(const T& what, std::vector<char>& buf_) {
			buf_.insert(buf_.end(), (const char*)& what, ((const char*)& what) + sizeof(what));
		}
	};

	std::map<std::string, std::string> headers;
private:
	
	int return_code_ = 200;
	int reserve_pos_ = -1;
	size_t reserved_size_ = 0;
	std::vector<char> buf_;
	std::shared_ptr<Server::ServerProxy> server_;
};


template<>
struct Response::WriteDelegate<std::string> {
	static void Write(const std::string& what, std::vector<char>& buf_) {
		WriteDelegate<uint32_t>::Write(static_cast<uint32_t>(what.size()) + 1, buf_); // include '\0'
		buf_.insert(buf_.end(), &what[0], &what.back() + 2);
	}
};

template<>
struct Response::WriteDelegate<const char*&> {
	static void Write(const char*& str, std::vector<char>& buf_) {
		if (str == nullptr) {
			WriteDelegate<uint32_t>::Write(1, buf_);
			buf_.push_back('\0');
			return;
		}
		uint32_t len = static_cast<uint32_t>(strlen(str)) + 1;
		WriteDelegate<uint32_t>::Write(len, buf_); // include '\0'
		buf_.insert(buf_.end(), str, str + len);
	}
};