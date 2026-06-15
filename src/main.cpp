#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

WebServer server(80);
File fsUploadFile;
unsigned long lastHeartbeat = 0;

// Отдача главного интерфейса DOS
void handleIndex() {
    File file = LittleFS.open("/index.html", "r");
    if (file) { 
        server.streamFile(file, "text/html"); 
        file.close(); 
    } else {
        server.send(500, "text/plain", "Error: index.html not found");
    }
}

// Пинг-сигнал от браузера для удержания сессии (Heartbeat)
void handleHeartbeat() {
    lastHeartbeat = millis();
    server.send(200, "text/plain", "alive");
}

// Получение списка файлов и папок текущего пути в формате JSON
void handleListDir() {
    if (!server.hasArg("path")) { 
        server.send(400, "text/plain", "Missing path"); 
        return; 
    }
    String targetPath = server.arg("path");

    String json = "[";
    File root = LittleFS.open("/", "r");
    String foundDirs = "";
    bool first = true;

    if (root && root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
            String fullPath = String(file.name());
            if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;

            if (fullPath.startsWith(targetPath) && fullPath != "/index.html" && fullPath != targetPath) {
                String subPath = fullPath.substring(targetPath.length());
                int slashIdx = subPath.indexOf("/");

                if (slashIdx != -1) {
                    String dirName = subPath.substring(0, slashIdx);
                    if (foundDirs.indexOf("[" + dirName + "]") == -1) {
                        foundDirs += "[" + dirName + "]";
                        if (!first) json += ",";
                        json += "{\"name\":\"" + dirName + "\",\"type\":\"dir\"}";
                        first = false;
                    }
                } else {
                    if (!first) json += ",";
                    json += "{\"name\":\"" + subPath + "\",\"type\":\"file\"}";
                    first = false;
                }
            }
            file = root.openNextFile();
        }
    }
    json += "]";
    server.send(200, "application/json", json);
}

// Создание пустой виртуальной папки через маркер
void handleCreateDir() {
    if (server.hasArg("path") && server.hasArg("name")) {
        String currentPath = server.arg("path");
        String folderName = server.arg("name");
        
        String markerPath = currentPath + folderName + "/.folder_marker";
        File file = LittleFS.open(markerPath, "w");
        if (file) { 
            file.print("dir"); 
            file.close(); 
        }
        server.send(200, "text/plain", "Directory Created");
    } else {
        server.send(400, "text/plain", "Bad Args");
    }
}

// Парсинг тега <address = "..."> из заголовка файла
String parseAddressFromFile(String path) {
    File file = LittleFS.open(path, "r");
    if (!file) return "";
    String buffer = "";
    while (file.available() && buffer.length() < 500) { 
        buffer += (char)file.read(); 
    }
    file.close();

    int startIdx = buffer.indexOf("<address = \"");
    if (startIdx != -1) {
        startIdx += 12;
        int endIdx = buffer.indexOf("\"", startIdx);
        if (endIdx != -1) { 
            return buffer.substring(startIdx, endIdx); 
        }
    }
    return "";
}

// Обработка загрузки файлов по структуре папок
void handleFileUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        fsUploadFile = LittleFS.open("/temp.html", "w");            
    } 
    else if (upload.status == UPLOAD_FILE_WRITE) {
        if (fsUploadFile) {
            fsUploadFile.write(upload.buf, upload.currentSize);
        }
    } 
    else if (upload.status == UPLOAD_FILE_END) {
        if (fsUploadFile) {
            fsUploadFile.close();
            
            String customAddr = parseAddressFromFile("/temp.html");
            String currentDir = server.arg("current_dir");
            if (currentDir == "") currentDir = "/";

            if (customAddr.length() > 0) {
                String finalPath = currentDir + customAddr + ".html";
                if (LittleFS.exists(finalPath)) LittleFS.remove(finalPath);
                LittleFS.rename("/temp.html", finalPath);
                Serial.printf("[DOS] Файл сохранен: %s\n", finalPath.c_str());
            } else {
                LittleFS.remove("/temp.html");
                Serial.println("[DOS] Ошибка: Тег адреса отсутствует!");
            }
        }
    }
}

// Поиск и отдача кастомных файлов (Динамический роутинг)
void handleNotFound() {
    String uri = server.uri();
    
    File root = LittleFS.open("/", "r");
    if (root && root.isDirectory()) {
        File file = root.openNextFile();
        while (file) {
            String fullPath = String(file.name());
            if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;
            
            if (fullPath.endsWith(uri + ".html")) {
                server.streamFile(file, "text/html");
                file.close();
                return;
            }
            file = root.openNextFile();
        }
    }
    server.send(404, "text/plain", "File Not Found inside Retro Executive");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n[SYSTEM] Инициализация LittleFS...");
    if (!LittleFS.begin(true)) {
        Serial.println("[SYSTEM] Ошибка монтирования LittleFS!");
        return;
    }

    Serial.println("[SYSTEM] Запуск Wi-Fi Точки Доступа...");
    // Настраиваем сеть
    WiFi.mode(WIFI_AP);
    bool ap_result = WiFi.softAP("SandboxWEB");
    
    if (ap_result) {
        Serial.print("[SYSTEM] Сеть успешно запущена! IP: ");
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("[SYSTEM] Критическая ошибка запуска Wi-Fi!");
    }

    // Настройка роутов веб-сервера
    server.on("/", HTTP_GET, handleIndex);
    server.on("/list-dir", HTTP_GET, handleListDir);
    server.on("/create-dir", HTTP_POST, handleCreateDir);
    server.on("/heartbeat", HTTP_GET, handleHeartbeat);
    
    server.on("/upload", HTTP_POST, []() {
        server.sendHeader("Location", "/", true);
        server.send(302, "text/plain", "Done");
    }, handleFileUpload);

    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("[SYSTEM] HTTP Веб-сервер готов.");
}

void loop() {
    server.handleClient();
}
