// SPDX-FileCopyrightText: 2026 fila73
// SPDX-FileContributor: Inspired by Red Salamander
// SPDX-License-Identifier: GPL-2.0-or-later

#include "precomp.h"
#include "sharepoint_auth.h"
#include "sharepoint_cache.h"
#include "sharepoint_http.h"
#include "sharepoint_json.h"
#include "sharepoint_log.h"
#include <algorithm>

namespace SharePointAuth
{

static const wchar_t* kRegSubKey = L"Software\\Altap\\Salamander\\Plugins\\sharepoint";
static const wchar_t* kRegAccountsSubKey = L"Software\\Altap\\Salamander\\Plugins\\sharepoint\\Accounts";
static const wchar_t* kRegValActiveAccount = L"ActiveAccount";
static const wchar_t* kRegValRefreshToken = L"EncryptedRefreshToken";
static const wchar_t* kRegValEncryptedClientSecret = L"EncryptedClientSecret";
static const wchar_t* kRegValClientId = L"OAuthClientId";
static const wchar_t* kRegValTenantId = L"OAuthTenantId";
static const wchar_t* kRegValClientSecret = L"OAuthClientSecret";
static const wchar_t* kRegValAccountEmail = L"AccountEmail";
static const wchar_t* kRegValAccountName = L"AccountName";

static bool DpapiEncrypt(const std::string& plainText, const wchar_t* desc, std::vector<BYTE>& outBytes)
{
    if (plainText.empty()) return false;
    DATA_BLOB inBlob{};
    inBlob.pbData = (BYTE*)plainText.data();
    inBlob.cbData = (DWORD)plainText.length();

    DATA_BLOB outBlob{};
    if (!CryptProtectData(&inBlob, desc, NULL, NULL, NULL, 0, &outBlob))
    {
        return false;
    }
    outBytes.assign(outBlob.pbData, outBlob.pbData + outBlob.cbData);
    LocalFree(outBlob.pbData);
    return true;
}

static bool DpapiDecrypt(const BYTE* pData, DWORD cbData, std::string& outPlainText)
{
    if (!pData || cbData == 0) return false;
    DATA_BLOB inBlob{};
    inBlob.pbData = const_cast<BYTE*>(pData);
    inBlob.cbData = cbData;

    DATA_BLOB outBlob{};
    if (!CryptUnprotectData(&inBlob, NULL, NULL, NULL, NULL, 0, &outBlob))
    {
        return false;
    }
    outPlainText.assign((const char*)outBlob.pbData, outBlob.cbData);
    LocalFree(outBlob.pbData);
    return true;
}

static std::wstring MakeSafeAccountKey(const std::string& email)
{
    std::string safe = email.empty() ? "default" : email;
    std::replace(safe.begin(), safe.end(), '@', '_');
    std::replace(safe.begin(), safe.end(), '.', '_');
    std::replace(safe.begin(), safe.end(), ':', '_');
    std::replace(safe.begin(), safe.end(), '/', '_');
    std::replace(safe.begin(), safe.end(), '\\', '_');
    return SharePointHttp::HttpClient::Utf8ToWide(safe);
}

AuthManager& AuthManager::GetInstance()
{
    static AuthManager instance;
    return instance;
}

AuthManager::AuthManager()
    : m_clientId(kDefaultClientId),
      m_tenantId(kDefaultTenantId),
      m_clientSecret(kDefaultClientSecret)
{
    LoadSavedTokens();
}

AuthManager::~AuthManager()
{
}

void AuthManager::Initialize(const std::string& customClientId,
                             const std::string& customTenantId,
                             const std::string& customClientSecret)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!customClientId.empty())
        m_clientId = customClientId;
    else
        m_clientId = kDefaultClientId;

    if (!customTenantId.empty())
        m_tenantId = customTenantId;
    else
        m_tenantId = kDefaultTenantId;

    if (!customClientSecret.empty())
        m_clientSecret = customClientSecret;
    else
        m_clientSecret = kDefaultClientSecret;
}

void AuthManager::SetClientId(const std::string& id)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (id.empty())
        m_clientId = kDefaultClientId;
    else
        m_clientId = id;
}

void AuthManager::SetTenantId(const std::string& tenant)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (tenant.empty())
        m_tenantId = kDefaultTenantId;
    else
        m_tenantId = tenant;
}

void AuthManager::SetClientSecret(const std::string& secret)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (secret.empty())
        m_clientSecret = kDefaultClientSecret;
    else
        m_clientSecret = secret;
}

bool AuthManager::IsAuthenticated()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_tokens.refreshToken.empty() || !m_tokens.accessToken.empty();
}

std::string AuthManager::GetAccountDisplay() const
{
    if (!m_tokens.accountEmail.empty())
        return m_tokens.accountEmail;
    if (!m_tokens.accountName.empty())
        return m_tokens.accountName;
    if (!m_tokens.refreshToken.empty() || !m_tokens.accessToken.empty())
        return "Connected";
    return "Not connected";
}

std::string AuthManager::GetValidAccessToken(std::string* errorOut)
{
    std::string refreshToken;
    std::string clientId;
    std::string tenantId;
    std::string clientSecret;

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (!m_tokens.accessToken.empty() && m_tokens.expiresAt > (now + 60))
        {
            return m_tokens.accessToken;
        }

        refreshToken = m_tokens.refreshToken;
        clientId = m_clientId;
        tenantId = m_tenantId.empty() ? kDefaultTenantId : m_tenantId;
        clientSecret = m_clientSecret;
    }

    if (!refreshToken.empty())
    {
        if (clientId.empty())
        {
            if (errorOut) *errorOut = "No Client ID configured";
            return "";
        }

        SharePointHttp::HttpClient http;
        std::string tokenUrl = "https://login.microsoftonline.com/" + tenantId + "/oauth2/v2.0/token";

        std::string body = "client_id=" + SharePointHttp::HttpClient::UrlEncode(clientId) +
                           "&scope=" + SharePointHttp::HttpClient::UrlEncode(kOAuthScope) +
                           "&refresh_token=" + SharePointHttp::HttpClient::UrlEncode(refreshToken) +
                           "&grant_type=refresh_token";

        if (!clientSecret.empty())
        {
            body += "&client_secret=" + SharePointHttp::HttpClient::UrlEncode(clientSecret);
        }

        auto resp = http.Post(tokenUrl, body, "application/x-www-form-urlencoded");
        if (resp.success)
        {
            auto json = SharePointJson::Value::Parse(resp.body);
            std::string newAccessToken = json.GetString("access_token");
            int64_t expiresIn = json.GetInt64("expires_in", 3600);
            std::string newRefreshToken = json.GetString("refresh_token");

            if (!newAccessToken.empty())
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_tokens.accessToken = newAccessToken;
                m_tokens.expiresAt = now + expiresIn;
                if (!newRefreshToken.empty())
                {
                    m_tokens.refreshToken = newRefreshToken;
                }
                SaveTokens();
                return m_tokens.accessToken;
            }
            else
            {
                if (errorOut) *errorOut = "Failed to parse access_token from refresh response";
            }
        }
        else
        {
            if (errorOut) *errorOut = "Token refresh failed: " + resp.body;
        }
    }

    if (errorOut && errorOut->empty())
    {
        *errorOut = "No valid authentication tokens available. Please sign in.";
    }
    return "";
}

bool AuthManager::RefreshAccessTokenInternal(std::string* errorOut)
{
    return !GetValidAccessToken(errorOut).empty();
}

std::string AuthManager::Base64UrlEncode(const unsigned char* data, size_t length)
{
    static const char base64_chars[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-_";

    std::string ret;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (length--)
    {
        char_array_3[i++] = *(data++);
        if (i == 3)
        {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; (i < 4); i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i)
    {
        for (int j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (int j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];
    }

    return ret;
}

std::string AuthManager::UrlDecode(const std::string& in)
{
    std::string out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); ++i)
    {
        if (in[i] == '%' && i + 2 < in.length())
        {
            int h1 = in[i + 1];
            int h2 = in[i + 2];
            auto hexVal = [](int c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int v1 = hexVal(h1);
            int v2 = hexVal(h2);
            if (v1 >= 0 && v2 >= 0)
            {
                out += static_cast<char>((v1 << 4) | v2);
                i += 2;
                continue;
            }
        }
        else if (in[i] == '+')
        {
            out += ' ';
            continue;
        }
        out += in[i];
    }
    return out;
}

std::string AuthManager::GenerateRandomString(size_t length)
{
    std::vector<unsigned char> randomBytes(length);
    HCRYPTPROV hProv = 0;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        CryptGenRandom(hProv, (DWORD)length, randomBytes.data());
        CryptReleaseContext(hProv, 0);
    }
    else
    {
        for (size_t i = 0; i < length; ++i)
            randomBytes[i] = (unsigned char)(rand() % 256);
    }
    return Base64UrlEncode(randomBytes.data(), randomBytes.size()).substr(0, length);
}

std::string AuthManager::ComputePkceChallenge(const std::string& verifier)
{
    unsigned char hash[32] = {0};
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;

    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
    {
        if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash))
        {
            CryptHashData(hHash, (const BYTE*)verifier.data(), (DWORD)verifier.length(), 0);
            DWORD hashLen = sizeof(hash);
            CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);
            CryptDestroyHash(hHash);
        }
        CryptReleaseContext(hProv, 0);
    }

    return Base64UrlEncode(hash, sizeof(hash));
}

bool AuthManager::FetchUserInfo(const std::string& accessToken)
{
    SharePointHttp::HttpClient http;
    auto resp = http.Get("https://graph.microsoft.com/v1.0/me", accessToken);
    if (resp.success)
    {
        auto json = SharePointJson::Value::Parse(resp.body);
        if (json.IsObject())
        {
            std::string email = json.GetString("mail");
            if (email.empty()) email = json.GetString("userPrincipalName");
            m_tokens.accountEmail = email;
            m_tokens.accountName = json.GetString("displayName");
            return true;
        }
    }
    return false;
}

bool AuthManager::LaunchInteractiveAuth(HWND hParent, std::string* errorOut)
{
    std::string clientId;
    std::string tenantId;
    std::string clientSecret;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        clientId = m_clientId;
        tenantId = m_tenantId.empty() ? kDefaultTenantId : m_tenantId;
        clientSecret = m_clientSecret;
    }

    if (clientId.empty())
    {
        if (errorOut) *errorOut = "Please configure an Application (client) ID first in Settings.";
        return false;
    }

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        if (errorOut) *errorOut = "Failed to initialize Winsock";
        return false;
    }

    // Create IPv4 loopback socket
    SOCKET listenSock4 = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock4 == INVALID_SOCKET)
    {
        WSACleanup();
        if (errorOut) *errorOut = "Failed to create IPv4 listener socket";
        return false;
    }

    sockaddr_in serverAddr4{};
    serverAddr4.sin_family = AF_INET;
    serverAddr4.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serverAddr4.sin_port = 0; // auto assign port

    if (bind(listenSock4, (sockaddr*)&serverAddr4, sizeof(serverAddr4)) == SOCKET_ERROR)
    {
        closesocket(listenSock4);
        WSACleanup();
        if (errorOut) *errorOut = "Failed to bind IPv4 loopback port";
        return false;
    }

    sockaddr_in boundAddr4{};
    int boundAddrLen4 = sizeof(boundAddr4);
    getsockname(listenSock4, (sockaddr*)&boundAddr4, &boundAddrLen4);
    int port = ntohs(boundAddr4.sin_port);

    if (listen(listenSock4, 5) == SOCKET_ERROR)
    {
        closesocket(listenSock4);
        WSACleanup();
        if (errorOut) *errorOut = "Failed to listen on IPv4 loopback port";
        return false;
    }

    // Also bind IPv6 loopback socket on the SAME port (for browsers resolving localhost -> ::1)
    SOCKET listenSock6 = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock6 != INVALID_SOCKET)
    {
        DWORD v6only = 1;
        setsockopt(listenSock6, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&v6only, sizeof(v6only));
        sockaddr_in6 serverAddr6{};
        serverAddr6.sin6_family = AF_INET6;
        serverAddr6.sin6_addr = in6addr_loopback;
        serverAddr6.sin6_port = htons(port);
        if (bind(listenSock6, (sockaddr*)&serverAddr6, sizeof(serverAddr6)) == SOCKET_ERROR ||
            listen(listenSock6, 5) == SOCKET_ERROR)
        {
            closesocket(listenSock6);
            listenSock6 = INVALID_SOCKET;
        }
    }

    // Set sockets non-blocking for select() timeout handling
    u_long nonBlocking = 1;
    ioctlsocket(listenSock4, FIONBIO, &nonBlocking);
    if (listenSock6 != INVALID_SOCKET)
    {
        ioctlsocket(listenSock6, FIONBIO, &nonBlocking);
    }

    std::string codeVerifier = GenerateRandomString(64);
    std::string codeChallenge = ComputePkceChallenge(codeVerifier);
    std::string state = GenerateRandomString(32);
    std::string redirectUri = "http://localhost:" + std::to_string(port);

    std::string authUrl = "https://login.microsoftonline.com/" + tenantId + "/oauth2/v2.0/authorize"
                          "?client_id=" + SharePointHttp::HttpClient::UrlEncode(clientId) +
                          "&response_type=code" +
                          "&redirect_uri=" + SharePointHttp::HttpClient::UrlEncode(redirectUri) +
                          "&response_mode=query" +
                          "&scope=" + SharePointHttp::HttpClient::UrlEncode(kOAuthScope) +
                          "&state=" + SharePointHttp::HttpClient::UrlEncode(state) +
                          "&code_challenge=" + SharePointHttp::HttpClient::UrlEncode(codeChallenge) +
                          "&code_challenge_method=S256" +
                          "&prompt=select_account";

    SharePointLog::Log("OAuth: Launching browser on port %d", port);
    ShellExecuteA(NULL, "open", authUrl.c_str(), NULL, NULL, SW_SHOWNORMAL);

    std::string authCode;
    auto startTime = std::chrono::steady_clock::now();
    bool received = false;

    while (!received)
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime).count();
        if (elapsed > 180) // 3 minutes timeout
        {
            if (errorOut) *errorOut = "Authentication timed out. Browser authorization was not completed.";
            break;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSock4, &readSet);
        if (listenSock6 != INVALID_SOCKET)
        {
            FD_SET(listenSock6, &readSet);
        }

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 200000; // 200ms

        int sel = select(0, &readSet, NULL, NULL, &tv);
        if (sel > 0)
        {
            SOCKET readySock = INVALID_SOCKET;
            if (FD_ISSET(listenSock4, &readSet)) readySock = listenSock4;
            else if (listenSock6 != INVALID_SOCKET && FD_ISSET(listenSock6, &readSet)) readySock = listenSock6;

            if (readySock != INVALID_SOCKET)
            {
                SOCKET clientSock = accept(readySock, NULL, NULL);
                if (clientSock != INVALID_SOCKET)
                {
                    // Set client socket to blocking with a 5 second timeout
                    u_long clientNonBlocking = 0;
                    ioctlsocket(clientSock, FIONBIO, &clientNonBlocking);
                    DWORD rcvTimeout = 5000;
                    setsockopt(clientSock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&rcvTimeout, sizeof(rcvTimeout));

                    std::string req;
                    char reqBuf[4096];
                    while (true)
                    {
                        int bytesRead = recv(clientSock, reqBuf, sizeof(reqBuf), 0);
                        if (bytesRead <= 0) break;
                        req.append(reqBuf, bytesRead);
                        if (req.find("\r\n\r\n") != std::string::npos || req.find("\n\n") != std::string::npos)
                            break;
                        if (req.length() > 65536)
                            break;
                    }

                    SharePointLog::Log("OAuth: Received HTTP request (%u bytes)", (unsigned int)req.length());

                    if (!req.empty())
                    {
                        size_t qmark = req.find('?');
                        size_t endPos = req.find(" HTTP/", qmark != std::string::npos ? qmark : 0);

                        if (qmark != std::string::npos && endPos != std::string::npos && endPos > qmark)
                        {
                            std::string query = req.substr(qmark + 1, endPos - (qmark + 1));
                            std::stringstream ss(query);
                            std::string param;
                            std::string recvState;

                            while (std::getline(ss, param, '&'))
                            {
                                size_t eq = param.find('=');
                                if (eq != std::string::npos)
                                {
                                    std::string key = param.substr(0, eq);
                                    std::string val = UrlDecode(param.substr(eq + 1));
                                    if (key == "code") authCode = val;
                                    else if (key == "state") recvState = val;
                                }
                            }

                            if (!authCode.empty() && recvState == state)
                            {
                                received = true;
                                SharePointLog::Log("OAuth: Authorization code successfully captured!");
                            }
                        }

                        // Send response HTML back to browser
                        std::string htmlBody =
                            "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Open Salamander - SharePoint</title>"
                            "<style>body{font-family:Segoe UI,sans-serif;background:#1e1e1e;color:#fff;text-align:center;padding-top:60px;}"
                            "h1{color:#00a4ef;}.box{background:#2d2d2d;display:inline-block;padding:30px 50px;border-radius:8px;box-shadow:0 4px 12px rgba(0,0,0,0.5);}</style></head>"
                            "<body><div class='box'><h1>Prihlaseni uspesne! / Sign-in successful!</h1>"
                            "<p>Muzete zavrit toto okno a vratit se do Open Salamandera.<br>"
                            "You can close this tab and return to Open Salamander.</p></div></body></html>";

                        std::string resp = "HTTP/1.1 200 OK\r\n"
                                           "Content-Type: text/html; charset=utf-8\r\n"
                                           "Content-Length: " + std::to_string(htmlBody.length()) + "\r\n"
                                           "Connection: close\r\n\r\n" + htmlBody;

                        send(clientSock, resp.c_str(), (int)resp.length(), 0);
                    }
                    shutdown(clientSock, SD_BOTH);
                    closesocket(clientSock);
                }
            }
        }

        // Process Windows messages to prevent IDE / Salamander freezing
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    closesocket(listenSock4);
    if (listenSock6 != INVALID_SOCKET)
    {
        closesocket(listenSock6);
    }
    WSACleanup();

    if (!received || authCode.empty())
    {
        if (errorOut && errorOut->empty()) *errorOut = "Authorization code was not received";
        return false;
    }

    // Exchange auth code for tokens
    SharePointHttp::HttpClient http;
    std::string tokenUrl = "https://login.microsoftonline.com/" + tenantId + "/oauth2/v2.0/token";

    std::string body = "client_id=" + SharePointHttp::HttpClient::UrlEncode(clientId) +
                       "&scope=" + SharePointHttp::HttpClient::UrlEncode(kOAuthScope) +
                       "&code=" + SharePointHttp::HttpClient::UrlEncode(authCode) +
                       "&redirect_uri=" + SharePointHttp::HttpClient::UrlEncode(redirectUri) +
                       "&grant_type=authorization_code" +
                       "&code_verifier=" + SharePointHttp::HttpClient::UrlEncode(codeVerifier);

    if (!clientSecret.empty())
    {
        body += "&client_secret=" + SharePointHttp::HttpClient::UrlEncode(clientSecret);
    }

    auto resp = http.Post(tokenUrl, body, "application/x-www-form-urlencoded");
    if (!resp.success)
    {
        if (errorOut) *errorOut = "Token exchange failed: " + resp.body;
        return false;
    }

    auto json = SharePointJson::Value::Parse(resp.body);
    std::string accessToken = json.GetString("access_token");
    std::string refreshToken = json.GetString("refresh_token");
    int64_t expiresIn = json.GetInt64("expires_in", 3600);

    if (accessToken.empty())
    {
        if (errorOut) *errorOut = "No access_token returned by identity provider";
        return false;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_tokens.accessToken = accessToken;
        m_tokens.refreshToken = refreshToken;
        m_tokens.expiresAt = now + expiresIn;
        m_tokens.tenantId = tenantId;
    }

    FetchUserInfo(accessToken);
    SaveTokens();

    return true;
}

void AuthManager::Logout()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_tokens = AuthTokens();
    SaveTokens();
}

bool AuthManager::SaveTokens()
{
    HKEY hKey = NULL;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS)
    {
        return false;
    }

    RegSetValueExW(hKey, kRegValClientId, 0, REG_SZ,
                   (const BYTE*)SharePointHttp::HttpClient::Utf8ToWide(m_clientId).c_str(),
                   (DWORD)(SharePointHttp::HttpClient::Utf8ToWide(m_clientId).length() + 1) * sizeof(wchar_t));

    RegSetValueExW(hKey, kRegValTenantId, 0, REG_SZ,
                   (const BYTE*)SharePointHttp::HttpClient::Utf8ToWide(m_tenantId).c_str(),
                   (DWORD)(SharePointHttp::HttpClient::Utf8ToWide(m_tenantId).length() + 1) * sizeof(wchar_t));

    RegSetValueExW(hKey, kRegValAccountEmail, 0, REG_SZ,
                   (const BYTE*)SharePointHttp::HttpClient::Utf8ToWide(m_tokens.accountEmail).c_str(),
                   (DWORD)(SharePointHttp::HttpClient::Utf8ToWide(m_tokens.accountEmail).length() + 1) * sizeof(wchar_t));

    RegSetValueExW(hKey, kRegValAccountName, 0, REG_SZ,
                   (const BYTE*)SharePointHttp::HttpClient::Utf8ToWide(m_tokens.accountName).c_str(),
                   (DWORD)(SharePointHttp::HttpClient::Utf8ToWide(m_tokens.accountName).length() + 1) * sizeof(wchar_t));

    RegSetValueExW(hKey, kRegValActiveAccount, 0, REG_SZ,
                   (const BYTE*)SharePointHttp::HttpClient::Utf8ToWide(m_tokens.accountEmail).c_str(),
                   (DWORD)(SharePointHttp::HttpClient::Utf8ToWide(m_tokens.accountEmail).length() + 1) * sizeof(wchar_t));

    if (!m_tokens.refreshToken.empty())
    {
        std::vector<BYTE> encrypted;
        if (DpapiEncrypt(m_tokens.refreshToken, L"SharePointRefreshToken", encrypted))
        {
            RegSetValueExW(hKey, kRegValRefreshToken, 0, REG_BINARY, encrypted.data(), (DWORD)encrypted.size());
        }
    }
    else
    {
        RegDeleteValueW(hKey, kRegValRefreshToken);
    }

    if (!m_clientSecret.empty())
    {
        std::vector<BYTE> encryptedSecret;
        if (DpapiEncrypt(m_clientSecret, L"SharePointClientSecret", encryptedSecret))
        {
            RegSetValueExW(hKey, kRegValEncryptedClientSecret, 0, REG_BINARY, encryptedSecret.data(), (DWORD)encryptedSecret.size());
        }
    }
    else
    {
        RegDeleteValueW(hKey, kRegValEncryptedClientSecret);
    }

    RegCloseKey(hKey);

    // Save to multi-account storage
    if (!m_tokens.accountEmail.empty())
    {
        std::wstring accSub = std::wstring(kRegAccountsSubKey) + L"\\" + MakeSafeAccountKey(m_tokens.accountEmail);
        HKEY hAccKey = NULL;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, accSub.c_str(), 0, NULL, 0, KEY_WRITE, NULL, &hAccKey, NULL) == ERROR_SUCCESS)
        {
            RegSetValueExW(hAccKey, kRegValAccountEmail, 0, REG_SZ,
                           (const BYTE*)SharePointHttp::HttpClient::Utf8ToWide(m_tokens.accountEmail).c_str(),
                           (DWORD)(SharePointHttp::HttpClient::Utf8ToWide(m_tokens.accountEmail).length() + 1) * sizeof(wchar_t));

            RegSetValueExW(hAccKey, kRegValAccountName, 0, REG_SZ,
                           (const BYTE*)SharePointHttp::HttpClient::Utf8ToWide(m_tokens.accountName).c_str(),
                           (DWORD)(SharePointHttp::HttpClient::Utf8ToWide(m_tokens.accountName).length() + 1) * sizeof(wchar_t));

            RegSetValueExW(hAccKey, kRegValClientId, 0, REG_SZ,
                           (const BYTE*)SharePointHttp::HttpClient::Utf8ToWide(m_clientId).c_str(),
                           (DWORD)(SharePointHttp::HttpClient::Utf8ToWide(m_clientId).length() + 1) * sizeof(wchar_t));

            RegSetValueExW(hAccKey, kRegValTenantId, 0, REG_SZ,
                           (const BYTE*)SharePointHttp::HttpClient::Utf8ToWide(m_tenantId).c_str(),
                           (DWORD)(SharePointHttp::HttpClient::Utf8ToWide(m_tenantId).length() + 1) * sizeof(wchar_t));

            if (!m_tokens.refreshToken.empty())
            {
                std::vector<BYTE> encrypted;
                if (DpapiEncrypt(m_tokens.refreshToken, L"SharePointRefreshToken", encrypted))
                {
                    RegSetValueExW(hAccKey, kRegValRefreshToken, 0, REG_BINARY, encrypted.data(), (DWORD)encrypted.size());
                }
            }
            if (!m_clientSecret.empty())
            {
                std::vector<BYTE> encryptedSecret;
                if (DpapiEncrypt(m_clientSecret, L"SharePointClientSecret", encryptedSecret))
                {
                    RegSetValueExW(hAccKey, kRegValEncryptedClientSecret, 0, REG_BINARY, encryptedSecret.data(), (DWORD)encryptedSecret.size());
                }
            }
            RegCloseKey(hAccKey);
        }
    }

    return true;
}

bool AuthManager::LoadSavedTokens()
{
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegSubKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
    {
        return false;
    }

    wchar_t strBuf[512] = {0};
    DWORD bufSize = sizeof(strBuf);

    if (RegGetValueW(hKey, NULL, kRegValClientId, RRF_RT_REG_SZ, NULL, strBuf, &bufSize) == ERROR_SUCCESS)
    {
        m_clientId = SharePointHttp::HttpClient::WideToUtf8(strBuf);
    }

    bufSize = sizeof(strBuf);
    if (RegGetValueW(hKey, NULL, kRegValTenantId, RRF_RT_REG_SZ, NULL, strBuf, &bufSize) == ERROR_SUCCESS)
    {
        m_tenantId = SharePointHttp::HttpClient::WideToUtf8(strBuf);
    }
    if (m_tenantId.empty()) m_tenantId = kDefaultTenantId;

    bufSize = sizeof(strBuf);
    if (RegGetValueW(hKey, NULL, kRegValAccountEmail, RRF_RT_REG_SZ, NULL, strBuf, &bufSize) == ERROR_SUCCESS)
    {
        m_tokens.accountEmail = SharePointHttp::HttpClient::WideToUtf8(strBuf);
    }

    bufSize = sizeof(strBuf);
    if (RegGetValueW(hKey, NULL, kRegValAccountName, RRF_RT_REG_SZ, NULL, strBuf, &bufSize) == ERROR_SUCCESS)
    {
        m_tokens.accountName = SharePointHttp::HttpClient::WideToUtf8(strBuf);
    }

    DWORD binSize = 0;
    if (RegGetValueW(hKey, NULL, kRegValRefreshToken, RRF_RT_REG_BINARY, NULL, NULL, &binSize) == ERROR_SUCCESS && binSize > 0)
    {
        std::vector<BYTE> encBytes(binSize);
        if (RegGetValueW(hKey, NULL, kRegValRefreshToken, RRF_RT_REG_BINARY, NULL, encBytes.data(), &binSize) == ERROR_SUCCESS)
        {
            DpapiDecrypt(encBytes.data(), (DWORD)encBytes.size(), m_tokens.refreshToken);
        }
    }

    binSize = 0;
    if (RegGetValueW(hKey, NULL, kRegValEncryptedClientSecret, RRF_RT_REG_BINARY, NULL, NULL, &binSize) == ERROR_SUCCESS && binSize > 0)
    {
        std::vector<BYTE> encBytes(binSize);
        if (RegGetValueW(hKey, NULL, kRegValEncryptedClientSecret, RRF_RT_REG_BINARY, NULL, encBytes.data(), &binSize) == ERROR_SUCCESS)
        {
            DpapiDecrypt(encBytes.data(), (DWORD)encBytes.size(), m_clientSecret);
        }
    }

    RegCloseKey(hKey);
    return !m_tokens.refreshToken.empty();
}

std::vector<AccountProfile> AuthManager::GetAccounts()
{
    std::vector<AccountProfile> list;
    HKEY hAccountsKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegAccountsSubKey, 0, KEY_READ, &hAccountsKey) != ERROR_SUCCESS)
    {
        if (!m_tokens.accountEmail.empty())
        {
            AccountProfile p;
            p.email = m_tokens.accountEmail;
            p.displayName = m_tokens.accountName;
            p.clientId = m_clientId;
            p.tenantId = m_tenantId;
            p.clientSecret = m_clientSecret;
            p.isActive = true;
            list.push_back(p);
        }
        return list;
    }

    DWORD index = 0;
    wchar_t subKeyName[256];
    DWORD subKeyLen = sizeof(subKeyName) / sizeof(subKeyName[0]);

    while (RegEnumKeyExW(hAccountsKey, index++, subKeyName, &subKeyLen, NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
    {
        subKeyLen = sizeof(subKeyName) / sizeof(subKeyName[0]);
        HKEY hAcc = NULL;
        if (RegOpenKeyExW(hAccountsKey, subKeyName, 0, KEY_READ, &hAcc) == ERROR_SUCCESS)
        {
            AccountProfile profile;
            wchar_t valBuf[512] = {0};
            DWORD valSize = sizeof(valBuf);

            if (RegGetValueW(hAcc, NULL, kRegValAccountEmail, RRF_RT_REG_SZ, NULL, valBuf, &valSize) == ERROR_SUCCESS)
                profile.email = SharePointHttp::HttpClient::WideToUtf8(valBuf);

            valSize = sizeof(valBuf);
            if (RegGetValueW(hAcc, NULL, kRegValAccountName, RRF_RT_REG_SZ, NULL, valBuf, &valSize) == ERROR_SUCCESS)
                profile.displayName = SharePointHttp::HttpClient::WideToUtf8(valBuf);

            valSize = sizeof(valBuf);
            if (RegGetValueW(hAcc, NULL, kRegValClientId, RRF_RT_REG_SZ, NULL, valBuf, &valSize) == ERROR_SUCCESS)
                profile.clientId = SharePointHttp::HttpClient::WideToUtf8(valBuf);

            valSize = sizeof(valBuf);
            if (RegGetValueW(hAcc, NULL, kRegValTenantId, RRF_RT_REG_SZ, NULL, valBuf, &valSize) == ERROR_SUCCESS)
                profile.tenantId = SharePointHttp::HttpClient::WideToUtf8(valBuf);

            DWORD binSize = 0;
            if (RegGetValueW(hAcc, NULL, kRegValEncryptedClientSecret, RRF_RT_REG_BINARY, NULL, NULL, &binSize) == ERROR_SUCCESS && binSize > 0)
            {
                std::vector<BYTE> encBytes(binSize);
                if (RegGetValueW(hAcc, NULL, kRegValEncryptedClientSecret, RRF_RT_REG_BINARY, NULL, encBytes.data(), &binSize) == ERROR_SUCCESS)
                {
                    DpapiDecrypt(encBytes.data(), (DWORD)encBytes.size(), profile.clientSecret);
                }
            }

            profile.isActive = (_stricmp(profile.email.c_str(), m_tokens.accountEmail.c_str()) == 0);
            if (!profile.email.empty())
            {
                list.push_back(profile);
            }
            RegCloseKey(hAcc);
        }
    }
    RegCloseKey(hAccountsKey);

    return list;
}

bool AuthManager::SwitchAccount(const std::string& email)
{
    if (email.empty()) return false;

    std::wstring accSub = std::wstring(kRegAccountsSubKey) + L"\\" + MakeSafeAccountKey(email);
    HKEY hAcc = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, accSub.c_str(), 0, KEY_READ, &hAcc) != ERROR_SUCCESS)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_tokens = AuthTokens();

    wchar_t valBuf[512] = {0};
    DWORD valSize = sizeof(valBuf);

    if (RegGetValueW(hAcc, NULL, kRegValAccountEmail, RRF_RT_REG_SZ, NULL, valBuf, &valSize) == ERROR_SUCCESS)
        m_tokens.accountEmail = SharePointHttp::HttpClient::WideToUtf8(valBuf);

    valSize = sizeof(valBuf);
    if (RegGetValueW(hAcc, NULL, kRegValAccountName, RRF_RT_REG_SZ, NULL, valBuf, &valSize) == ERROR_SUCCESS)
        m_tokens.accountName = SharePointHttp::HttpClient::WideToUtf8(valBuf);

    valSize = sizeof(valBuf);
    if (RegGetValueW(hAcc, NULL, kRegValClientId, RRF_RT_REG_SZ, NULL, valBuf, &valSize) == ERROR_SUCCESS)
        m_clientId = SharePointHttp::HttpClient::WideToUtf8(valBuf);

    valSize = sizeof(valBuf);
    if (RegGetValueW(hAcc, NULL, kRegValTenantId, RRF_RT_REG_SZ, NULL, valBuf, &valSize) == ERROR_SUCCESS)
        m_tenantId = SharePointHttp::HttpClient::WideToUtf8(valBuf);

    DWORD binSize = 0;
    if (RegGetValueW(hAcc, NULL, kRegValRefreshToken, RRF_RT_REG_BINARY, NULL, NULL, &binSize) == ERROR_SUCCESS && binSize > 0)
    {
        std::vector<BYTE> encBytes(binSize);
        if (RegGetValueW(hAcc, NULL, kRegValRefreshToken, RRF_RT_REG_BINARY, NULL, encBytes.data(), &binSize) == ERROR_SUCCESS)
        {
            DpapiDecrypt(encBytes.data(), (DWORD)encBytes.size(), m_tokens.refreshToken);
        }
    }

    binSize = 0;
    if (RegGetValueW(hAcc, NULL, kRegValEncryptedClientSecret, RRF_RT_REG_BINARY, NULL, NULL, &binSize) == ERROR_SUCCESS && binSize > 0)
    {
        std::vector<BYTE> encBytes(binSize);
        if (RegGetValueW(hAcc, NULL, kRegValEncryptedClientSecret, RRF_RT_REG_BINARY, NULL, encBytes.data(), &binSize) == ERROR_SUCCESS)
        {
            DpapiDecrypt(encBytes.data(), (DWORD)encBytes.size(), m_clientSecret);
        }
    }

    RegCloseKey(hAcc);
    SaveTokens();
    return true;
}

bool AuthManager::RemoveAccount(const std::string& email)
{
    if (email.empty()) return false;

    std::wstring accSub = std::wstring(kRegAccountsSubKey) + L"\\" + MakeSafeAccountKey(email);
    RegDeleteKeyW(HKEY_CURRENT_USER, accSub.c_str());

    if (_stricmp(m_tokens.accountEmail.c_str(), email.c_str()) == 0)
    {
        Logout();
    }
    return true;
}

bool AuthManager::AddAccount(HWND hParent, std::string* errorOut)
{
    return LaunchInteractiveAuth(hParent, errorOut);
}

bool AuthManager::SaveAccountKeys(const std::string& email,
                                  const std::string& clientId,
                                  const std::string& tenantId,
                                  const std::string& clientSecret)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clientId = clientId;
    m_tenantId = tenantId.empty() ? kDefaultTenantId : tenantId;
    m_clientSecret = clientSecret;
    return SaveTokens();
}

} // namespace SharePointAuth
