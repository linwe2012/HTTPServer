# Minimal C++ HTTP Server

![IMG](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=c%2B%2B) ![IMG](https://img.shields.io/badge/Visual%20Studio-2019-5C2D91?&logo=visual-studio)

This implements minimum multithread node.js API in C++.

`std::thread` is used to create a simple pool.

Usage:

```c++
HttpServer app;

app.Get("/", [](auto preq, auto pres) {
    Request& req = *preq;
    Response& res = *pres;

    res.SendFile('Index.html')
});


app.Get("/post", [](auto preq, auto pres) {
    Request& req = *preq;
    Response& res = *pres;

    auto form = req.ParseForm();

   if(CheckCredentials(form["username"], form["password"])) {
       res.End("Login Success");
   }
   else {
       res.End("Login Failed");
   }
});

```