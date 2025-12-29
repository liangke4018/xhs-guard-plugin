#pragma once
#include <QTcpServer>
#include <QTcpSocket>
#include <QObject>

class HttpServer : public QTcpServer {
	Q_OBJECT
public:
	explicit HttpServer(QObject *parent = nullptr);
	bool start(quint16 port = 18888);

protected:
	void incomingConnection(qintptr socketDescriptor) override;

private slots:
	void handleReadyRead();
	void handleDisconnected();

private:
	void sendResponse(QTcpSocket *socket, int statusCode, const QByteArray &body);
};