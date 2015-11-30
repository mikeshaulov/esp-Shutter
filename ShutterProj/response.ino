void sendServerResponse(const char* responseStr, int errorCode = 200)
{
  webServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  webServer.sendHeader("Pragma", "no-cache");
  webServer.sendHeader("Expires", "-1");
  webServer.send(errorCode, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves.
  webServer.sendContent(responseStr);
  webServer.client().stop();
}

void handleRollUp()
{
  shutter.turnUp();
  sendServerResponse("Rolling Up");
}

void handleRollDown()
{
  shutter.turnDown();
  sendServerResponse("Rolling Down");
}

void handleOff()
{
  shutter.turnOff();
  sendServerResponse("Off");
}
