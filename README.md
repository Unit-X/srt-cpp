# cppSRTWrapper

*Simple C++ wrapper of the [SRT](https://github.com/Haivision/srt) protocol*.

The SRT Protocol is a UDP based protocol. SRT acts as a mix of TCP and UDP where SRT tries to re-send lost data (as TCP would) but only to a dedline in time specified, when the deadline is reached and if the data has not arrived it's marked as lost (as UDP would).

Streaming media solutions benefit from this approach and SRT is adoppted widly across the media comunity. This C++ wrapper is simplifying implementations of SRT in C++ projects and with only a few lines of code you can create a server and/or a client. 

The API of SRT has more features than what's exposed in this C++ wrapper, however the base functions exists. If you feel this wrapper is missing any functionalty or features please let me know.
 

**Current auto build status:**

![Ubuntu 18.04](https://github.com/andersc/cppSRTWrapper/workflows/Ubuntu%2018.04/badge.svg)

![macos](https://github.com/andersc/cppSRTWrapper/workflows/macos/badge.svg)

![Windows x64](https://github.com/andersc/cppSRTWrapper/workflows/Windows%20x64/badge.svg)

## Building

Requires cmake version >= **3.10** and **C++17**

*Linux and MacOS* ->

**Release:**

```sh
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

***Debug:***

```sh
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . --config Debug
```

Output: 

**libsrtnet.a**

The static SRTWrapper C++ wrapper library 
 
**cppSRTWrapper**

*cppSRTWrapper* (executable) runs trough the unit tests and returns EXIT_SUCESS if all unit tests pass.

-

# Building for windows

* Prepare your system by installing latest powershell (min version 3.0)

* Follow the instructions and install **chocolatey** -> [https://chocolatey.org](https://chocolatey.org)

* Install dependencies

```sh
choco install openssl
choco install cmake
choco install git
```

Clone this repository to a directory of your choice.

Then inside that directory (only needed once) ->


```sh
wget https://www.nuget.org/api/v2/package/cinegy.pthreads-win64-2015/2.9.1.24 -OutFile pthreads.zip
Expand-Archive -LiteralPath  .\pthreads.zip
```

**Build->**

**Release:**

```sh
mkdir build
cd build
cmake -DCMAKE_GENERATOR_PLATFORM=x64 -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

**Debug:**

```sh
mkdir build
cd build
cmake -DCMAKE_GENERATOR_PLATFORM=x64 -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . --config Debug
```


## Using the SRT wrapper

###Server:

```cpp

//Create the server
SRTNet mySRTNetServer;

//Register the server callbacks
//The validate connection callback ->
mySRTNetServer.clientConnected=std::bind(&validateConnection, std::placeholders::_1, std::placeholders::_2);
//The got data from client callback
mySRTNetServer.recievedData=std::bind(&handleData, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);

//Start the server

//Listen IP
//Listen Port
//Number packet reorder window
//Max re-send window (ms) / also the delay of transmission
//% extra of the BW that will be allowed for re-transmission packets
//MTU
//(Optional) AES-128 key (No key or empty == no encryption)

if (!mySRTNetServer.startServer("0.0.0.0", 8000, 16, 1000, 100, 1456,"Th1$_is_4_0pt10N4L_P$k")) {
   std::cout << "SRT Server failed to start." << std::endl;
   return EXIT_FAILURE;
}

//Send data to a client (you need the client handle)
mySRTNetServer.sendData(content->data(), content->size(), &thisMSGCTRL,clientHandle);

//Get client handles using the lambda 
mySRTNetServer.getActiveClients([](std::map<SRTSOCKET, std::shared_ptr<NetworkConnection>> &clientList)
  {
    std::cout << "The server got " << clientList.size() << " clients." << std::endl;
  }
); 



//When a client connects you can validate the connection and also embedd one of your objects into that SRT connection.

//Return a connection object. (Return nullptr if you don't want to connect to that client)
std::shared_ptr<NetworkConnection> validateConnection(struct sockaddr &sin, SRTSOCKET newSocket) {

//sin contains the connections properties
//newSocket contains the SRT handle for that particular client

//To reject the connection
return nullptr;

//Accepting the connection by returning a 'NetworkConnection'
//A NetworkConnection contains a 'object' where you can put a shared pointer 
//To any of your objects. When the connection drops from that
//Client the destructor is called as long as you do not also keep a 
//handle to that class. 
//In all communication with you the 'NetworkConnection' clas is also provided.
//You can throw generators or parsers in there if wanted (MPEG-TS or anything else)
//See the example for how to retreive the object back.

auto a1 = std::make_shared<NetworkConnection>();
a1->object = std::make_shared<MyClass>();
return a1;

}


/
```

###Client:

```cpp

//Create the client
SRTNet mySRTNetClient;

//Register the callback (The server sends data to the client)
mySRTNetClient.recievedData=std::bind(&handleDataClient, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4);

//Connect to a server

//Server IP
//Server Port
//Number packet reorder window
//Max re-send window (ms) / also the delay of transmission
//% extra of the BW that will be allowed for re-transmission packets
//MTU
//(Optional) AES-128 key (No key or empty == no encryption)
if (!mySRTNetClient.startClient("127.0.0.1", 8000, 16, 1000, 100,client1Connection, 1456,"Th1$_is_4_0pt10N4L_P$k")) {
    std::cout << "SRT client failed starting." << std::endl;
    return EXIT_FAILURE;
}

//Send data to the server
SRT_MSGCTRL thisMSGCTRL = srt_msgctrl_default;
mySRTNetClient.sendData(buffer2.data(), buffer2.size(), &thisMSGCTRL);

```

## Credits

The SRT team for all the help and positive feedback 

Anders Cedronius for creating the C++ wrapper

anders.cedronius(at)gmail.com


## License

*MIT*

Read *LICENCE.md* for details