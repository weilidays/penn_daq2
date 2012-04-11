#ifndef _XL3_H
#define _XL3_H

#include "NetUtils.h"
#include "GenericCallback.h"

class XL3 : public GenericCallback {
  public:
    XL3(int crateNum);
    ~XL3();

    void AcceptCallback(struct evconnlistener *listener, evutil_socket_t fd, struct sockaddr *address, int socklen);
    void RecvCallback(struct bufferevent *bev);
    void SentCallback(struct bufferevent *bev){};
    void EventCallback(struct bufferevent *bev, short what){};

  private:
    int fCrateNum;
    struct evconnlistener *fListener;
    int fFD;
    struct bufferevent *fBev;

};

#endif