#include "HttpServer.h"
#include "AudioController.h"
#include <QUrl>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

HttpServer::HttpServer(QObject *parent) : QTcpServer(parent) {}

bool HttpServer::start(quint16 port)
{
	return this->listen(QHostAddress::Any, port);
}

void HttpServer::incomingConnection(qintptr socketDescriptor)
{
	QTcpSocket *socket = new QTcpSocket(this);
	socket->setSocketDescriptor(socketDescriptor);
	connect(socket, &QTcpSocket::readyRead, this, &HttpServer::handleReadyRead);
	connect(socket, &QTcpSocket::disconnected, this, &HttpServer::handleDisconnected);
}

void HttpServer::handleReadyRead()
{
	QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
	if (!socket)
		return;

	QByteArray data = socket->readAll();
	QString request = QString::fromUtf8(data);
	QStringList lines = request.split("\r\n");
	if (lines.isEmpty())
		return;

	QString firstLine = lines[0];
	QStringList parts = firstLine.split(" ");
	if (parts.size() < 2)
		return;

	QString method = parts[0];
	QUrl url(parts[1]);
	QString path = url.path();

	if (method == "OPTIONS") {
		sendResponse(socket, 200, "{\"status\":\"ok\"}");
		return;
	}

	QJsonObject responseJson;

	if (path == "/play") {
		QUrlQuery query(url.query());
		QString audioPath = QUrl::fromPercentEncoding(query.queryItemValue("path").toUtf8());
		if (!audioPath.isEmpty()) {
			QString pickedFile = AudioController::instance().enqueueTaskAndReturn(audioPath, "reply");
			if (!pickedFile.isEmpty()) {
				responseJson["status"] = "success";
				responseJson["file"] = pickedFile;
				sendResponse(socket, 200, QJsonDocument(responseJson).toJson());
			} else {
				responseJson["status"] = "error";
				responseJson["message"] = "no_valid_audio_file_found";
				sendResponse(socket, 404, QJsonDocument(responseJson).toJson());
			}
		} else {
			responseJson["status"] = "error";
			responseJson["message"] = "missing_path_parameter";
			sendResponse(socket, 400, QJsonDocument(responseJson).toJson());
		}
	} else if (path == "/status") {
		// 🎯 核心修改：收到 Chrome 请求，记录心跳
		AudioController::instance().recordHeartbeat();
		responseJson["status"] = "online";
		sendResponse(socket, 200, QJsonDocument(responseJson).toJson());
	} else {
		responseJson["status"] = "error";
		responseJson["message"] = "route_not_found";
		sendResponse(socket, 404, QJsonDocument(responseJson).toJson());
	}
}

void HttpServer::sendResponse(QTcpSocket *socket, int statusCode, const QByteArray &body)
{
	if (socket->state() != QAbstractSocket::ConnectedState)
		return;
	socket->write("HTTP/1.1 " + QByteArray::number(statusCode) + " OK\r\n");
	socket->write("Content-Type: application/json; charset=utf-8\r\n");
	socket->write("Content-Length: " + QByteArray::number(body.size()) + "\r\n");
	socket->write("Access-Control-Allow-Origin: *\r\n");
	socket->write("Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n");
	socket->write("Access-Control-Allow-Headers: Content-Type\r\n");
	socket->write("Connection: close\r\n");
	socket->write("\r\n");
	socket->write(body);
	socket->flush();
	socket->disconnectFromHost();
}

void HttpServer::handleDisconnected()
{
	QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
	if (socket)
		socket->deleteLater();
}