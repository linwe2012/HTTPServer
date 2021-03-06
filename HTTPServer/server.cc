#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS

#pragma comment(lib, "Ws2_32.lib")

#else
#endif // _WIN32

#include <stdint.h>
#include <exception>
#include <string>
#include <chrono>

#include "server.h"
#include <stdlib.h>
#include <signal.h>
#include <list>
#include <time.h>
#include <fstream>

Server* current_server = nullptr;
void shutdown_server(int sig) { // can be called asynchronously
	current_server->Shutdown();
}

std::string GetIPString(const sockaddr_in& addr) {
	// https://stackoverflow.com/questions/1705885/why-inet-ntoa-is-designed-to-be-a-non-reentrant-function
	// use a thread local buffer, hence thread safe
	char buf[30];
	inet_ntop(AF_INET, &addr.sin_addr.S_un, buf, sizeof(buf));
	return
		buf +
		std::string(":") +
		std::to_string(ntohs(addr.sin_port));
}

int Server::Listen(int port) {
	current_server = this;
	signal(SIGINT, shutdown_server);
	port_ = port;

	int ret;
#ifdef _WIN32
	WSADATA wsaData;
	ret = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (ret != 0) throw "WSAStartup() failed!";
#endif // _WIN32

	sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	server_sock.sin_family = AF_INET;
	server_sock.sin_port = htons(port);
	server_sock.sin_addr.S_un.S_addr = htonl(INADDR_ANY);

	bind(sockfd, (struct sockaddr*) & server_sock, sizeof(server_sock));

	listen(sockfd, 5);

	printf("Server listens on %d\n", port);
	//����һ���㱨��ǰ״̬��lambda����
	report_status_ = [](int, std::string msg, bool) {
		std::cout << msg << std::endl;
	};

	set_tag_ = [](int, std::string, std::string n) {
		std::cout << "Tag info: " << n;
	};

	std::future<void> future;
	if (cli_hogging_) {
		future = std::async([this] {
			CLIMoniter();
		});
	}

	while (!should_close_)
	{
		struct sockaddr_in client_addr;
		int client_addrlen = sizeof(client_addr);
		int connect_fd;
		//���ֽ������ݰ�
		connect_fd = accept(sockfd, (struct sockaddr*) & client_addr, &client_addrlen);

		if (connect_fd == -1) {
			Error("failed incoming connection");

			if (should_close_) {
				break;
			}
			else {
				continue;
			}
		}
		//ÿ�ӵ�һ����ַ�����ݱ㽫���������
		clients_[connect_fd] = client_addr;
		auto pclient_addr = &clients_[connect_fd];//��ÿͻ��˵�ַ��ָ��
		pool_.Schedule([this, pclient_addr, connect_fd] {
			ProcessRequest(connect_fd, pclient_addr);//���̳߳��з����߳�
		});
	}
	pool_.Terminate();
	ret = WSACleanup();
	return ret;
}

#ifndef SHUT_RD
#define SHUT_RD SD_RECEIVE
#endif // !SHUT_RD


void Server::Shutdown()
{

	should_close_ = true;
	closesocket(sockfd);
}

void Server::ProcessRequest(int connect_fd, sockaddr_in* client_addr)
{
	report_status_(connect_fd, "Get Conneted from " + GetIPString(*client_addr), false);//������ڽ������ݵĵ�ַ
	//�趨һ�����ݴ�����Ϣ��lambda����
	auto Fatal = [this, connect_fd](std::string what) {
		report_status_(connect_fd, what, true);
		CloseConnection(connect_fd);
	};

	auto FatalSend = [this, connect_fd, &Fatal](std::string what) {
		try {
			//TODO
			//SendError(connect_fd, what);
		}
		catch (std::exception & e) {
			Fatal(e.what());
		}
	};

	struct timeval timeout;
	timeout.tv_sec = 60;
	timeout.tv_usec = 0;

	//���ɴ���
	auto eop = std::shared_ptr<ServerProxy>(new ServerProxy(this, connect_fd, client_addr));
	eop->EnableCleanup();

	uint32_t last_left = 0;
	std::vector<char> buffer(4096);

	//while (!should_close_)
	{
		uint32_t nrecved = 0;
		int ret = -1;
		bool first_time_recv = true;
		bool recover_flag = false;
		nrecved = last_left;
		last_left = 0;
		//���Ȼ�����ݰ��Ĵ�С
		while (nrecved < sizeof(uint32_t))
		{
			ret = recv(connect_fd, &buffer.front() + nrecved, static_cast<uint32_t>(buffer.size()) - nrecved-1, 0);
			if (ret == -1) {
				if (errno == EWOULDBLOCK) {
					Fatal("Connection time out.");
				}
				else {
					Fatal("Unable to receive");
				}
				return;
			}

			if (ret == 0) {
				if (first_time_recv == true) {
					report_status_(connect_fd, "Client closed connection", true);
					return;
				}
				break;
			}
			first_time_recv = false;
			nrecved += ret;
		}
		//���������Ϣ
		if (nrecved < sizeof(uint32_t)) {
			FatalSend("expected size larger than uint32");
			//continue;
			return;
		}

		report_status_(connect_fd, "Receiving Data: " + GetIPString(*client_addr), false);//������ڽ������ݵĵ�ַ
		

		try {
			handler_(//ִ�лص�����
				std::shared_ptr<Request>(new Request(eop, buffer.data(), nrecved+1)),
				std::shared_ptr<Response>(new Response(eop, buffer.data(), nrecved+1))
			);
		}
		catch (std::exception & e) {
			report_status_(connect_fd, e.what(), true);
			//continue;
			return;
		}
	}
}

//������Ӳ��������ݰ�
void Server::SendSync(uint32_t ip, int port, char* buffer, uint32_t size)
{
	struct sockaddr_in client;
	client.sin_family = AF_INET;
	client.sin_port = port;
	client.sin_addr.S_un.S_addr = ip;
	int ret;
	ret = connect(sendfd, (struct sockaddr*) & client, sizeof(client));
	if (ret == -1) {
		throw std::exception("Connect failed");
	}
	ret = send(sendfd, buffer, size, 0);
	if (ret == -1) {
		throw std::exception("Connect failed");
	}

	return;
}
void Server::ReplySync(int fd, const char* buffer, uint32_t size)
{
	int ret = send(fd, buffer, size, 0);
	if (ret == -1) {
		throw std::exception("Reply: Connect failed");
	}
	return;
}


//�ر�����
void Server::CloseConnection(int connect_fd) {
	auto p = clients_.find(connect_fd);

	if (p == clients_.end()) {
		Error("Closing non-exist client");
		return;
	}
	auto& c = p->second;

	std::stringstream ss;

	ss << "Connection(" << connect_fd << ") "
		<< GetIPString(c) << " closed";

	report_status_(connect_fd, ss.str(), true);

	clients_.erase(connect_fd);
	closesocket(connect_fd);

}
//��Ϣ�Ľṹ��
struct DyingMsg {
	sockaddr_in addr;
	std::string msg;
};

class outbuf : public std::streambuf {
	HANDLE h;
public:
	outbuf(HANDLE h) : h(h) {}
protected:
	virtual int_type overflow(int_type c) override {
		if (c != EOF) {
			DWORD written;
			WriteConsole(h, &c, 1, &written, nullptr);
		}
		return c;
	}

	virtual std::streamsize xsputn(char_type const* s, std::streamsize count) override {
		DWORD written;
		WriteConsole(h, s, count, &written, nullptr);
		return written;
	}
};

//����һ������
struct Defer {
	Defer(std::function<void()> f) {
		f_ = f;
	}
	~Defer() {
		f_();
	}

	std::function<void()> f_;
};
void Server::CLIMoniter()
{
	auto& os = std::clog;
	//�����Ļ�������ľ�����豸�ľ��
	auto buffer1 = GetStdHandle(STD_OUTPUT_HANDLE);
	auto buffer2 = CreateConsoleScreenBuffer(GENERIC_WRITE, 0, NULL, CONSOLE_TEXTMODE_BUFFER, NULL);
	outbuf console_out1(buffer1);
	outbuf console_out2(buffer2);

	bool which_buffer = false;

	auto restore_buf = os.rdbuf(&console_out1);
	//����һ���Ƴٵķ���
	Defer defer([&restore_buf, &os] {
		os.rdbuf(restore_buf);
	});

	std::list<DyingMsg> dying_msg;
	std::map<uint64_t, std::vector<std::string>> status;//����һ��map
	//����㱨�õķ���
	report_status_ = [&](int connect_fd, std::string msg, bool is_closing) {
		if (is_closing) {
			if (status.find(connect_fd) != status.end()) {
				status.erase(connect_fd);
			}
			dying_msg.push_back({ clients_[connect_fd] , msg });
		}
		else {
			status[connect_fd].push_back(msg);
		}
	};
	//��õ�ǰ��ʱ��
	auto tick = std::chrono::system_clock::now();
	//ѡ������һ�������õĻ��������
	while (!should_close_)
	{
		if (which_buffer == false) {
			os.rdbuf(&console_out1);
			SetConsoleActiveScreenBuffer(buffer1);
		}
		else {
			os.rdbuf(&console_out2);
			SetConsoleActiveScreenBuffer(buffer2);
		}

		os << "\033[2J\033[1;1H";

		os << "Server listening at " << GetIPString(server_sock);//�����������ip
		auto span = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - tick);
		auto sec = span.count();
		auto min = sec / 60;
		sec = sec % 60;
		//��������ѽ��е�ʱ��
		os << "\t\tserver up: " << min << ":" << sec;
		os << "\n\n";
		//��������̳߳ص���Ϣ
		auto thread_status = pool_.GetStatus();
		os << "Thread Pool Info: \n   - number of threads:" << thread_status.num_threads
			<< "\n   - number of pending tasks:" << thread_status.num_pending_tasks;


		os << "\n   - all threads info";
		int cnt = 0;
		for (auto& info : thread_status.threads) {
			if (cnt % 2 == 0) {
				os << "\n\t|";
			}
			os << cnt << "\t|" << (info.is_busy ? "running" : "pending") << "\t|";
			++cnt;
		}

		os << "\n\nAll Connections Info\n";
		os << "   - num connections: " << clients_.size();
		os << "\n\nActive connection";
		for (const auto& c : clients_) {
			os << "\n" << c.first << "\t" << GetIPString(c.second);
			auto p = status.find(c.first);
			if (p != status.end()) {
				if (p->second.size()) {
					os << "\t" << p->second.back();
				}
			}
		}

		os << "\n\nClosed connection (recently 5 will shown)\n";
		cnt = 0;
		for (auto c = dying_msg.crbegin(); c != dying_msg.crend(); ++c) {
			os << (cnt == 0 ? "--> " : "    ");
			os << GetIPString(c->addr) << "     " << c->msg << "\n";
			++cnt;
			if (cnt == 5) {
				break;
			}
		}

		std::this_thread::sleep_for(1s);
	}
}


void Server::ServerProxy::SendSync(uint32_t ip, int port, char* buf, uint32_t size)
{
	server_->SendSync(ip, port, buf, size);
}

Server::ServerProxy::~ServerProxy() {
	if (cleanup_) {
		server_->CloseConnection(connect_fd_);
	}
}

const std::map<uint64_t, sockaddr_in>& Server::ServerProxy::GetConnectionInfo() {
	return server_->clients_;
}

void Server::ServerProxy::Relay(int fd, const char* buf, uint32_t size)
{
	server_->ReplySync(fd, buf, size);
}

void Server::ServerProxy::CloseConnection()
{
	server_->CloseConnection(connect_fd_);
}

void Server::ServerProxy::ReportStatus(std::string s)
{
	if (!server_->report_status_) {
		return;
	}
	server_->report_status_(connect_fd_, s, false);
}

//�޸����ݰ��Ĵ�С����������
void Response::End(std::string what)
{
	static std::map<int, std::string> ret_code = {
		{200, "OK"},
		{404, "NOT FOUND"}
	};
	std::stringstream ss;
	ss << "HTTP/1.1 " << return_code_ << " " << ret_code[return_code_] << "\r\n";
	for (auto& head : headers) {
		ss << head.first << ": " << head.second << "\r\n";
	}
	ss << "Content-Length: " << what.size() + buf_.size() << "\r\n";
	ss << "\r\n";
	std::string str = ss.str();

	buf_.insert(buf_.begin(), str.data(), str.data()+str.size());
	if (what.size()) {
		buf_.insert(buf_.end(), what.data(), what.data() + what.size());
	}

	server_->ReplySync(buf_.data(), buf_.size());
}


Response::Response(std::shared_ptr<Server::ServerProxy> _server, char* buffer, uint32_t buffer_size)
	: server_(_server), server(*_server)
{
	headers["Server"] = "Leon";
}



std::string Response::GetTimeString() {
	time_t current_time;
	struct tm* ptime;
	time(&current_time);
	ptime = gmtime(&current_time);

	static const char* week[] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};

	static const char* month[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};

	std::stringstream ss;
	ss << week[ptime->tm_wday] << ", "
		<< ptime->tm_mday << " "
		<< month[ptime->tm_mon] << " "
		<< (1900 + ptime->tm_year) << " "
		<< ptime->tm_hour << ":"
		<< ptime->tm_min << ":"
		<< ptime->tm_sec << " "
		<< "GMT";
	std::string str = ss.str();
	return str;
}

#include <filesystem>
namespace fs = std::filesystem;

std::string DeductType(fs::path& p) {
	static std::map<std::string, std::string> mime = {
		{"html", "text/html" },
		{"htm", "text/html" },
		{"htmls", "text/html"},
		{"png", "image/png" },
		{"jpg", "image/jpg" },
		{"js", "text/javascript"},
		{"css", "text/css" },
	};

	std::string ext = p.extension().string().substr(1);
	auto itr = mime.find(ext);
	if (itr != mime.end()) {
		return itr->second;
	}
	return "text/plain";
}

std::map<std::string, std::string> Request::ParseForm() {
	std::map<std::string, std::string> form;
	while (true)
	{
		auto key = stream.StringEndBy('=');
		if (key == nullptr) break;
		auto val = stream.StringEndByNoStrict('&');
		if (val == nullptr) break;
		form[key] = val;
	}
	return form;
}

Response& Response::SendFile(std::string path) {
	fs::path p = path;
	if (!fs::exists(p)) {
		return *this;
	}

	headers["Date"] = GetTimeString();
	headers["Content-Type"] = DeductType(p);


	FILE* fp = fopen(path.c_str(), "rb");
	

	char buf[256];
	while (!feof(fp))
	{
		int i = fread(buf, 1, 255, fp);
		buf_.insert(buf_.end(), buf, buf+i);
	}
	
	End();
	return *this;
}

