#include <objc/runtime.h>
#include <os/log.h>
#include "../core/core.hh"
#include "../ipc/ipc.hh"
#include "../window/window.hh"

using namespace SSC;

constexpr auto _settings = STR_VALUE(SSC_SETTINGS);
constexpr auto _debug = false;

static dispatch_queue_attr_t qos = dispatch_queue_attr_make_with_qos_class(
  DISPATCH_QUEUE_CONCURRENT,
  QOS_CLASS_USER_INITIATED,
  -1
);

static dispatch_queue_t queue = dispatch_queue_create(
  "co.socketsupply.queue.app",
  qos
);

@interface InputAccessoryHackHelper : NSObject
@end

@implementation InputAccessoryHackHelper
- (id) inputAccessoryView {
  return nil;
}
@end

@interface CorsDisablingURLSchemeHandler : NSObject <WKURLSchemeHandler, NSURLSessionTaskDelegate>
@property (nonatomic, strong) NSMutableDictionary<NSURLSessionTask *, id<WKURLSchemeTask>> *schemeTasks;
@property (nonatomic, strong) NSMutableDictionary<NSValue *, NSURLSessionDataTask *> *dataTasks;
@end

@implementation CorsDisablingURLSchemeHandler
- (instancetype)init {
  self = [super init];
  if (self) {
    self.schemeTasks = [NSMutableDictionary dictionary];
    self.dataTasks = [NSMutableDictionary dictionary];
  }
  return self;
}
- (void) webView: (WKWebView*) webView startURLSchemeTask: (id<WKURLSchemeTask>) schemeTask {
  NSURL *url = schemeTask.request.URL;
  url = [NSURL URLWithString:[url.absoluteString substringFromIndex:5]];

  NSMutableURLRequest *request = [schemeTask.request mutableCopy];
  request.URL = url;

  // Rewrite X-Origin, X-Referer, and X-Cookie headers to Origin, Referer, and Cookie.
  NSDictionary *headers = request.allHTTPHeaderFields;
  if (headers[@"X-Origin"] != nil) {
    [request setValue:headers[@"X-Origin"] forHTTPHeaderField:@"Origin"];
    [request setValue:nil forHTTPHeaderField:@"X-Origin"];
  }
  if (headers[@"X-Referer"] != nil) {
    [request setValue:headers[@"X-Referer"] forHTTPHeaderField:@"Referer"];
    [request setValue:nil forHTTPHeaderField:@"X-Referer"];
  }
  if (headers[@"X-Cookie"] != nil) {
    [request setValue:headers[@"X-Cookie"] forHTTPHeaderField:@"Cookie"];
    [request setValue:nil forHTTPHeaderField:@"X-Cookie"];
  }

  os_log(OS_LOG_DEFAULT, "Request: %@ %@", request, request.allHTTPHeaderFields);
  dispatch_async(queue, ^{
    NSURLSessionDataTask *dataTask = [[NSURLSession sharedSession] dataTaskWithRequest:request];
    [self.schemeTasks setObject:schemeTask forKey:dataTask];
    [self.dataTasks setObject:dataTask forKey:[NSValue valueWithPointer:schemeTask]];
    [dataTask setDelegate:self];
    [dataTask resume];
  });
}
- (void) webView: (WKWebView*) webView stopURLSchemeTask: (id<WKURLSchemeTask>) schemeTask {
  NSValue *schemeTaskKey = [NSValue valueWithPointer:schemeTask];
  NSURLSessionDataTask *dataTask = [_dataTasks objectForKey:schemeTaskKey];
  if (dataTask == nil) {
    return;
  }
  [_dataTasks removeObjectForKey:schemeTaskKey];
  [_schemeTasks removeObjectForKey:dataTask];
  [dataTask cancel];
}
- (void)URLSession:(NSURLSession *)session
              dataTask:(NSURLSessionDataTask *)dataTask
    didReceiveResponse:(NSURLResponse *)response
     completionHandler:
         (void (^)(NSURLSessionResponseDisposition))completionHandler {
  id<WKURLSchemeTask> schemeTask = [_schemeTasks objectForKey:dataTask];
  if (schemeTask == nil) {
    completionHandler(NSURLSessionResponseCancel);
    return;
  }

  // Set Access-Control-Allow-Origin response header.
  NSHTTPURLResponse *httpResponse = (NSHTTPURLResponse *)response;
  NSMutableDictionary *headers = [httpResponse.allHeaderFields mutableCopy];
  [headers setValue:@"*" forKey:@"Access-Control-Allow-Origin"];
  httpResponse = [[NSHTTPURLResponse alloc] initWithURL:httpResponse.URL
                                             statusCode:httpResponse.statusCode
                                            HTTPVersion:@"HTTP/1.1"
                                           headerFields:headers];

  os_log(OS_LOG_DEFAULT, "Response: %@", httpResponse);
  [schemeTask didReceiveResponse:httpResponse];

  completionHandler(NSURLSessionResponseAllow);
}
- (void)URLSession:(NSURLSession *)session
          dataTask:(NSURLSessionDataTask *)dataTask
    didReceiveData:(NSData *)data {
  id<WKURLSchemeTask> schemeTask = [_schemeTasks objectForKey:dataTask];
  if (schemeTask == nil) {
    return;
  }
  [schemeTask didReceiveData:data];
}
- (void)URLSession:(NSURLSession *)session
                    task:(NSURLSessionTask *)dataTask
    didCompleteWithError:(NSError *)error {
  id<WKURLSchemeTask> schemeTask = [_schemeTasks objectForKey:dataTask];
  if (schemeTask == nil) {
    return;
  }
  [_dataTasks removeObjectForKey:[NSValue valueWithPointer:schemeTask]];
  [_schemeTasks removeObjectForKey:dataTask];
  if (error == nil) {
    os_log(OS_LOG_DEFAULT, "Success");
    [schemeTask didFinish];
  } else {
    os_log(OS_LOG_DEFAULT, "Error: %@", error);
    [schemeTask didFailWithError:error];
  }
}
@end

@interface AppDelegate : UIResponder <
  UIApplicationDelegate,
  WKScriptMessageHandler,
  UIScrollViewDelegate
> {
  SSC::IPC::Bridge* bridge;
  Core* core;
}
@property (strong, nonatomic) UIWindow* window;
@property (strong, nonatomic) SSCNavigationDelegate* navDelegate;
@property (strong, nonatomic) SSCBridgedWebView* webview;
@property (strong, nonatomic) WKUserContentController* content;
@property (strong, nonatomic) NSTimer* pongTimeout;
@property (strong, nonatomic) NSTimer* pingInterval;
@property (nonatomic) BOOL pongAlertVisible;
@end

//
// iOS has no "window". There is no navigation, just a single webview. It also
// has no "main" process, we want to expose some network functionality to the
// JavaScript environment so it can be used by the web app and the wasm layer.
//
@implementation AppDelegate
- (void) applicationDidEnterBackground: (UIApplication*) application {
  [self.webview evaluateJavaScript: @"window.blur()" completionHandler: nil];
}

- (void) applicationWillEnterForeground: (UIApplication*) application {
  [self.webview evaluateJavaScript: @"window.focus()" completionHandler: nil];
  // bridge->bluetooth.startScanning();
}

- (void) applicationWillTerminate: (UIApplication*) application {
  // @TODO(jwerle): what should we do here?
}

- (void) applicationDidBecomeActive: (UIApplication*) application {
  dispatch_async(queue, ^{
    self->core->resumeAllPeers();
    self->core->runEventLoop();
  });
}

- (void) applicationWillResignActive: (UIApplication*) application {
  dispatch_async(queue, ^{
    self->core->stopEventLoop();
    self->core->pauseAllPeers();
  });
}

//
// When a message is received try to route it.
// Messages may also be received and routed via the custom scheme handler.
//
- (void) userContentController: (WKUserContentController*) userContentController
       didReceiveScriptMessage: (WKScriptMessage*) scriptMessage
{
  if ([scriptMessage.name isEqualToString:@"pong"]) {
    // NSLog(@"pong");
    [_pongTimeout invalidate];
    return;
  }

  id body = [scriptMessage body];

  if (![body isKindOfClass:[NSString class]]) {
    return;
  }

  bridge->route([body UTF8String], nullptr, 0);
}

- (void) keyboardWillHide {
  auto json = JSON::Object::Entries {
    {"value", JSON::Object::Entries {
      {"event", "will-hide"}
    }}
  };

  // self.webview.scrollView.scrollEnabled = YES;
  bridge->router.emit("keyboard", JSON::Object(json).str());
}

- (void) keyboardDidHide {
  auto json = JSON::Object::Entries {
    {"value", JSON::Object::Entries {
      {"event", "did-hide"}
    }}
  };

  bridge->router.emit("keyboard", JSON::Object(json).str());
}

- (void) keyboardWillShow {
  auto json = JSON::Object::Entries {
    {"value", JSON::Object::Entries {
      {"event", "will-show"}
    }}
  };

  // self.webview.scrollView.scrollEnabled = NO;
  bridge->router.emit("keyboard", JSON::Object(json).str());
}

- (void) keyboardDidShow {
  auto json = JSON::Object::Entries {
    {"value", JSON::Object::Entries {
      {"event", "did-show"}
    }}
  };

  bridge->router.emit("keyboard", JSON::Object(json).str());
}

- (void) keyboardWillChange: (NSNotification*) notification {
  NSDictionary* keyboardInfo = [notification userInfo];
  NSValue* keyboardFrameBegin = [keyboardInfo valueForKey: UIKeyboardFrameEndUserInfoKey];
  CGRect rect = [keyboardFrameBegin CGRectValue];
  CGFloat width = rect.size.width;
  CGFloat height = rect.size.height;

  auto json = JSON::Object::Entries {
    {"value", JSON::Object::Entries {
      {"event", "will-change"},
      {"width", width},
      {"height", height},
    }}
  };

  bridge->router.emit("keyboard", JSON::Object(json).str());
}

- (void) scrollViewDidScroll: (UIScrollView*) scrollView {
  scrollView.bounds = self.webview.bounds;
}

- (BOOL) application: (UIApplication*) app
             openURL: (NSURL*) url
             options: (NSDictionary<UIApplicationOpenURLOptionsKey, id>*) options
{
  // TODO can this be escaped or is the url encoded property already?
  auto json = JSON::Object::Entries {{"url", [url.absoluteString UTF8String]}};
  bridge->router.emit("protocol", JSON::Object(json).str());
  return YES;
}

-           (BOOL) application: (UIApplication*) application
 didFinishLaunchingWithOptions: (NSDictionary*) launchOptions
{
  using namespace SSC;

  platform.os = "ios";

  core = new Core;
  bridge = new IPC::Bridge(core);
  bridge->router.dispatchFunction = [=] (auto callback) {
    dispatch_async(queue, ^{ callback(); });
  };

  bridge->router.evaluateJavaScriptFunction = [=](auto js) {
    dispatch_async(dispatch_get_main_queue(), ^{
      auto script = [NSString stringWithUTF8String: js.c_str()];
      [self.webview evaluateJavaScript: script completionHandler: nil];
    });
  };

  NSNotificationCenter* ns = [NSNotificationCenter defaultCenter];
  [ns addObserver: self selector: @selector(keyboardDidShow) name: UIKeyboardDidShowNotification object: nil];
  [ns addObserver: self selector: @selector(keyboardDidHide) name: UIKeyboardDidHideNotification object: nil];
  [ns addObserver: self selector: @selector(keyboardWillShow) name: UIKeyboardWillShowNotification object: nil];
  [ns addObserver: self selector: @selector(keyboardWillHide) name: UIKeyboardWillHideNotification object: nil];
  [ns addObserver: self selector: @selector(keyboardWillChange:) name: UIKeyboardWillChangeFrameNotification object: nil];

  [self setUpWebView];
  [self keyboardDisplayDoesNotRequireUserAction];

  return YES;
}

- (void)webViewWebContentProcessDidTerminate:(WKWebView *)webView
{
  if (webView == self.webview) {
    [self setUpWebView];
  }
}

- (void)setUpWebView
{
  [self.pingInterval invalidate];
  [self.pongTimeout invalidate];
  [self.content removeScriptMessageHandlerForName: @"pong"];
  [self.content removeScriptMessageHandlerForName: @"external"];
  [self.content removeAllScriptMessageHandlers];
  self.webview.scrollView.delegate = nil;
  [self.webview stopLoading];
  [self.webview removeFromSuperview];

  auto appData = parseConfig(decodeURIComponent(_settings));

  StringStream env;

  for (auto const &envKey : split(appData["env"], ',')) {
    auto cleanKey = trim(envKey);
    auto envValue = getEnv(cleanKey.c_str());

    env << String(
      cleanKey + "=" + encodeURIComponent(envValue) + "&"
    );
  }

  auto appFrame = [[UIScreen mainScreen] bounds];

  env << String("width=" + std::to_string(appFrame.size.width) + "&");
  env << String("height=" + std::to_string(appFrame.size.height) + "&");

  NSFileManager *fileManager = [NSFileManager defaultManager];
  NSString *currentDirectoryPath = [fileManager currentDirectoryPath];
  NSString *cwd = [NSHomeDirectory() stringByAppendingPathComponent: currentDirectoryPath];

  WindowOptions opts {
    .debug = _debug,
    .env = env.str(),
    .cwd = String([cwd UTF8String])
  };

  // Note: you won't see any logs in the preload script before the
  // Web Inspector is opened
  String  preload = ToString(createPreload(opts));

  WKUserScript* initScript = [[WKUserScript alloc]
    initWithSource: [NSString stringWithUTF8String: preload.c_str()]
    injectionTime: WKUserScriptInjectionTimeAtDocumentStart
    forMainFrameOnly: NO];

  WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];

  [config setURLSchemeHandler: [CorsDisablingURLSchemeHandler new] forURLScheme: @"cors-http"];
  [config setURLSchemeHandler: [CorsDisablingURLSchemeHandler new] forURLScheme: @"cors-https"];
  [config setURLSchemeHandler: bridge->router.schemeHandler
                 forURLScheme: @"ipc"];

  self.content = [config userContentController];

  [self.content addScriptMessageHandler:self name: @"pong"];
  [self.content addScriptMessageHandler:self name: @"external"];
  [self.content addUserScript: initScript];

  self.webview = [[SSCBridgedWebView alloc] initWithFrame:appFrame configuration: config];
  self.webview.autoresizingMask = UIViewAutoresizingNone;
  self.webview.translatesAutoresizingMaskIntoConstraints = YES;
  self.webview.scrollView.delegate = self;
  self.webview.scrollView.scrollEnabled = NO;

  [self.webview.configuration.preferences setValue: @YES forKey: @"allowFileAccessFromFileURLs"];
  [self.webview.configuration.preferences setValue: @YES forKey: @"javaScriptEnabled"];

  self.navDelegate = [[SSCNavigationDelegate alloc] init];
  [self.webview setNavigationDelegate: self.navDelegate];

  [self removeKeyboardInputAccessoryView:self.webview];

  NSString* allowed = [[NSBundle mainBundle] resourcePath];
  NSURL* url;
  int port = getDevPort();

  if (isDebugEnabled() && port > 0 && getDevHost() != nullptr) {
    NSString* host = [NSString stringWithUTF8String:getDevHost()];
    url = [NSURL
      URLWithString: [NSString stringWithFormat: @"http://%@:%d/", host, port]
    ];

    [self.webview loadFileRequest: [NSURLRequest requestWithURL: url]
          allowingReadAccessToURL: [NSURL fileURLWithPath: allowed]
    ];
  } else {
    url = [NSURL
      fileURLWithPath: [allowed stringByAppendingPathComponent:@"ui/index.html"]
    ];

    [self.webview loadFileURL: url
      allowingReadAccessToURL: [NSURL fileURLWithPath:allowed]];
  }

  UIViewController *viewController = [[UIViewController alloc] init];
  viewController.view.frame = appFrame;
  [viewController.view addSubview: self.webview];

  self.window = [[UIWindow alloc] initWithFrame: appFrame];
  self.window.rootViewController = viewController;
  [self.window makeKeyAndVisible];

  if (isDebugEnabled()) {
    self.pingInterval = [NSTimer scheduledTimerWithTimeInterval:5.0
                                                        target:self
                                                      selector:@selector(ping)
                                                      userInfo:nil
                                                        repeats:YES];
  }
}

- (void)ping
{
  if (self.pongAlertVisible) {
    return;
  }
  self.pongTimeout = [NSTimer scheduledTimerWithTimeInterval:2.0
                                                      target:self
                                                    selector:@selector(webViewBecameUnresponsive)
                                                    userInfo:nil
                                                     repeats:NO];

  // NSLog(@"ping");
  [self.webview evaluateJavaScript:@"window.ping()" completionHandler:nil];
}

- (void)webViewBecameUnresponsive
{
  self.pongAlertVisible = YES;

  UIAlertController *alert = [UIAlertController
      alertControllerWithTitle:@"Error"
                       message:@"The web view is unresponsive. Should I reload "
                               @"it for you?"
                preferredStyle:UIAlertControllerStyleAlert];

  UIAlertAction *reloadAction =
      [UIAlertAction actionWithTitle:@"Reload"
                               style:UIAlertActionStyleDefault
                             handler:^(UIAlertAction *_Nonnull action) {
                               [self setPongAlertVisible:NO];
                               [self setUpWebView];
                             }];

  UIAlertAction *cancelAction =
      [UIAlertAction actionWithTitle:@"Cancel"
                               style:UIAlertActionStyleCancel
                             handler:^(UIAlertAction *_Nonnull action) {
                               [self setPongAlertVisible:NO];
                             }];

  [alert addAction:reloadAction];
  [alert addAction:cancelAction];

  [self.window.rootViewController presentViewController:alert
                                               animated:YES
                                             completion:nil];
}

// https://github.com/Telerik-Verified-Plugins/WKWebView/commit/04e8296adeb61f289f9c698045c19b62d080c7e3#L609-L620
- (void)keyboardDisplayDoesNotRequireUserAction {
  Class contentViewClass = NSClassFromString(@"WKContentView");
  NSOperatingSystemVersion iOS_11_3_0 = (NSOperatingSystemVersion){11, 3, 0};
  NSOperatingSystemVersion iOS_12_2_0 = (NSOperatingSystemVersion){12, 2, 0};
  NSOperatingSystemVersion iOS_13_0_0 = (NSOperatingSystemVersion){13, 0, 0};
  char *methodSignature =
      (char *)"_startAssistingNode:userIsInteracting:blurPreviousNode:"
              "changingActivityState:userObject:";

  if ([[NSProcessInfo processInfo]
          isOperatingSystemAtLeastVersion:iOS_13_0_0]) {
    methodSignature =
        (char *)"_elementDidFocus:userIsInteracting:blurPreviousNode:"
                "activityStateChanges:userObject:";
  } else if ([[NSProcessInfo processInfo]
                 isOperatingSystemAtLeastVersion:iOS_12_2_0]) {
    methodSignature =
        (char *)"_elementDidFocus:userIsInteracting:blurPreviousNode:"
                "changingActivityState:userObject:";
  }

  if ([[NSProcessInfo processInfo]
          isOperatingSystemAtLeastVersion:iOS_11_3_0]) {
    SEL selector = sel_getUid(methodSignature);
    Method method = class_getInstanceMethod(contentViewClass, selector);
    IMP original = method_getImplementation(method);
    IMP override = imp_implementationWithBlock(
        ^void(id me, void *arg0, BOOL arg1, BOOL arg2, BOOL arg3, id arg4) {
          ((void (*)(id, SEL, void *, BOOL, BOOL, BOOL, id))original)(
              me, selector, arg0, TRUE, arg2, arg3, arg4);
        });
    method_setImplementation(method, override);
  } else {
    SEL selector = sel_getUid(
        "_startAssistingNode:userIsInteracting:blurPreviousNode:userObject:");
    Method method = class_getInstanceMethod(contentViewClass, selector);
    IMP original = method_getImplementation(method);
    IMP override = imp_implementationWithBlock(
        ^void(id me, void *arg0, BOOL arg1, BOOL arg2, id arg3) {
          ((void (*)(id, SEL, void *, BOOL, BOOL, id))original)(
              me, selector, arg0, TRUE, arg2, arg3);
        });
    method_setImplementation(method, override);
  }
}

- (void)removeKeyboardInputAccessoryView:(WKWebView*)webview
{
  __block UIView *target;
  [[webview.scrollView subviews]
      enumerateObjectsUsingBlock:^(__kindof UIView *_Nonnull obj,
                                   NSUInteger idx, BOOL *_Nonnull stop) {
        *stop = [[NSString stringWithFormat:@"%@", [obj class]]
            hasPrefix:@"WKContent"];
        if (stop) {
          target = obj;
        }
      }];

  Class superclass = [target superclass];
  if (!target || !superclass) {
    return;
  }

  NSString *noInputAccessoryViewClassName =
      [NSString stringWithFormat:@"%@_NoInputAccessoryView", superclass];

  static Class newClass = NSClassFromString(noInputAccessoryViewClassName);
  if (!newClass) {
    Class targetClass = [target class];
    const char *classNameCString = [noInputAccessoryViewClassName
        cStringUsingEncoding:NSASCIIStringEncoding];

    newClass = objc_allocateClassPair(targetClass, classNameCString, 0);
    if (newClass) {
      objc_registerClassPair(newClass);
    }
  }

  if (!newClass) {
    return;
  }

  SEL originalMethodSelector = @selector(inputAccessoryView);
  Method originalMethod = class_getInstanceMethod(
      [InputAccessoryHackHelper class], originalMethodSelector);

  if (!originalMethod) {
    return;
  }

  class_addMethod(newClass, originalMethodSelector,
                  method_getImplementation(originalMethod),
                  method_getTypeEncoding(originalMethod));
  object_setClass(target, newClass);
}

@end

int main (int argc, char *argv[]) {
  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
  }
}
