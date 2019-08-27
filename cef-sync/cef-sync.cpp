// HTML 5 GUI Demo
// Copyright (c) 2019 Borislav Stanimirov
//
// Distributed under the MIT Software License
// See accompanying file LICENSE.txt or copy at
// https://opensource.org/licenses/MIT
//
#include <iostream>
#include <include/cef_app.h>
#include <include/cef_client.h>
#include <include/wrapper/cef_resource_manager.h>

#include <helper/DirUtil.hpp>

#include <jsbind.hpp>

#define URI_ROOT "http://htmldemos"
const char* const URL = URI_ROOT "/cef-echo.html";

void setupResourceManagerDirectoryProvider(CefRefPtr<CefResourceManager> resource_manager, std::string uri, std::string dir)
{
    if (!CefCurrentlyOn(TID_IO)) {
        // Execute on the browser IO thread.
        CefPostTask(TID_IO, base::Bind(&setupResourceManagerDirectoryProvider, resource_manager, uri, dir));
        return;
    }

    resource_manager->AddDirectoryProvider(uri, dir, 1, dir);
}

// this is only needed so we have a way to break the message loop
class MinimalClient : public CefClient, public CefLifeSpanHandler, public CefRequestHandler, public CefResourceRequestHandler
{
public:
    MinimalClient()
        : m_resourceManager(new CefResourceManager)
    {
        auto exePath = DirUtil::getCurrentExecutablePath();
        auto assetPath = DirUtil::getAssetPath(exePath, "html");
        setupResourceManagerDirectoryProvider(m_resourceManager, URI_ROOT, assetPath);
    }

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override
    {
        CefQuitMessageLoop();
    }

    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
        CefRefPtr<CefBrowser> /*browser*/,
        CefRefPtr<CefFrame> /*frame*/,
        CefRefPtr<CefRequest> /*request*/,
        bool /*is_navigation*/,
        bool /*is_download*/,
        const CefString& /*request_initiator*/,
        bool& /*disable_default_handling*/) override {
        return this;
    }

    cef_return_value_t OnBeforeResourceLoad(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request,
        CefRefPtr<CefRequestCallback> callback) override
    {
        return m_resourceManager->OnBeforeResourceLoad(browser, frame, request, callback);
    }

    CefRefPtr<CefResourceHandler> GetResourceHandler(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefRequest> request) override
    {
        return m_resourceManager->GetResourceHandler(browser, frame, request);
    }

private:
    CefRefPtr<CefResourceManager> m_resourceManager;

    IMPLEMENT_REFCOUNTING(MinimalClient);
    DISALLOW_COPY_AND_ASSIGN(MinimalClient);
};

jsbind::persistent jsOnReceiveFunc;

void setReceiveFunc(jsbind::local func)
{
    jsOnReceiveFunc.reset(func);
}

void echo(std::string text)
{
    std::cout << "Called echo with text: " << text << std::endl;
    jsOnReceiveFunc.to_local()(text);
}

JSBIND_BINDINGS(App)
{
    jsbind::function("echo", echo);
    jsbind::function("setReceiveFunc", setReceiveFunc);
}

class RendererApp : public CefApp, public CefRenderProcessHandler
{
public:
    RendererApp() = default;

    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override
    {
        return this;
    }

    void OnContextCreated(CefRefPtr<CefBrowser> /*browser*/, CefRefPtr<CefFrame> /*frame*/, CefRefPtr<CefV8Context> /*context*/) override
    {
        jsbind::initialize();
    }

    void OnContextReleased(CefRefPtr<CefBrowser> /*browser*/, CefRefPtr<CefFrame> /*frame*/, CefRefPtr<CefV8Context> /*context*/) override
    {
        jsbind::enter_context();
        jsOnReceiveFunc.reset();
        jsbind::exit_context();
        jsbind::deinitialize();
    }

private:
    IMPLEMENT_REFCOUNTING(RendererApp);
    DISALLOW_COPY_AND_ASSIGN(RendererApp);
};

int main(int argc, char* argv[])
{
    CefRefPtr<CefCommandLine> commandLine = CefCommandLine::CreateCommandLine();
#if defined(_WIN32)
    CefEnableHighDPISupport();
    CefMainArgs args(GetModuleHandle(NULL));
    commandLine->InitFromString(GetCommandLineW());
#else
    CefMainArgs args(argc, argv);
    commandLine->InitFromArgv(argc, argv);
#endif

    void* windowsSandboxInfo = NULL;

#if defined(CEF_USE_SANDBOX) && defined(_WIN32)
    // Manage the life span of the sandbox information object. This is necessary
    // for sandbox support on Windows. See cef_sandbox_win.h for complete details.
    CefScopedSandboxInfo scopedSandbox;
    windowsSandboxInfo = scopedSandbox.sandbox_info();
#endif

    CefRefPtr<CefApp> app = nullptr;
    std::string appType = commandLine->GetSwitchValue("type");
    if (appType == "renderer" || appType == "zygote" || appType.empty())
    {
        app = new RendererApp;
        // use nullptr for other process types
    }
    int result = CefExecuteProcess(args, app, windowsSandboxInfo);
    if (result >= 0)
    {
        // child process completed
        return result;
    }

    CefSettings settings;
#if !defined(CEF_USE_SANDBOX)
    settings.no_sandbox = true;
#endif

    CefInitialize(args, settings, app, windowsSandboxInfo);

    CefWindowInfo windowInfo;

#if defined(_WIN32)
    // On Windows we need to specify certain flags that will be passed to CreateWindowEx().
    windowInfo.SetAsPopup(NULL, "simple");
#endif
    CefBrowserSettings browserSettings;
    CefBrowserHost::CreateBrowser(windowInfo, new MinimalClient, URL, browserSettings, nullptr, nullptr);

    CefRunMessageLoop();

    CefShutdown();

    return 0;
}
