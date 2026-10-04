# mini-redis-cpp

Servidor TCP estilo Redis em C++17 com protocolo inline (uma linha é um comando). Sem dependência externa, mutex protege o mapa, TTL com expiração, teto de conexões e de linha. Resposta em formato RESP-lite.

## Por que eu fiz isso

Queria fechar o círculo: parser de PE lê binário morto, scanner lê memória morta, e
m que faltava era servir dado vivo. Construí um store key-value na mão com
thread por conexão e TTL funcionando.

## Como funciona

Accept loop cria thread por conexão destacada. Cada thread lê linha a linha até `\n`, valida
tamanho, quebra por espaço e converte o comando para upper. Store mantém
`unordered_map<string, Entry>` com mutex. GET e EXISTS removem chave expirada na
hora, SIZE limpa todas juntas no momento da consulta.

Comandos:

| Comando | Resposta |
|:--|:--|
| PING | +PONG |
| SET key val | +OK |
| SET key val EX n | +OK |
| GET key | $<len>\r\n<val> ou $-1 |
| DEL key | :1 ou :0 |
| EXISTS key | :1 ou :0 |
| SIZE | :n |
| QUIT | +OK |

## Segurança e limites

Teto de 128 conexões simultâneas, linha máxima 4096 bytes, valor máximo 1 MB,
TTL máximo 365 dias, timeout de leitura de 30 segundos por conexão.
Comando com argumentos errado devolve `-ERR wrong_number_of_arguments`.
Build com `-Wall -Wextra -Wpedantic -Werror`, fortify e stack protector.

## Estrutura

    src/
      main.cpp       servidor TCP e dispatch
      kv.cpp         store com mutex e TTL
    include/
      kv.hpp         tipos e limites
    CMakeLists.txt
    README.md

## Como compilar

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j
    ./build/mini-redis 6399

No Windows usa o mesmo CMakeLists no Visual Studio, linka ws2_32.

## Como testar

    python3 -c "
    import socket
    c = socket.create_connection(('127.0.0.1', 6399))
    c.sendall(b'SET hello world\r\nGET hello\r\n')
    print(c.recv(4096).decode())
    c.close()
    "

Retorno 0 em boot ok, 1 em porta inválida ou bind falhado.

## Testes validados neste repositório

    PING responde +PONG — OK
    SET guarda e GET devolve o valor — OK
    TTL EX 1 expira depois de 1 segundo (GET vira $-1) — OK
    DEL remove e GET depois vira $-1 — OK
    GET sem argumento devolve wrong_number_of_arguments — OK
    Comando desconhecido devolve -ERR unknown — OK
    SIZE reflete a quantidade de chaves vivas — OK

## Limitações

Store en memoria only, nada persistido. Sem RESP binário, só inline. Sem
replicação, clustering ou criptografia. Sem comandos Redis reais completos,
é um subconjunto didático. Vazamento lento de conexão se cliente parar de falar
antes do timeout.

## Próximos passos

Persistência em append-only file, comandos SETNX/GETSET, expiração ativa com
timer, RESP2/RESP3 completo, benchmark de throughput.

## Autor

Rafael Nunes — [@Rafaelnunes-alt2](https://github.com/Rafaelnunes-alt2)
