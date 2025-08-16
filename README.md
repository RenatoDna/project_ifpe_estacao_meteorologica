Estação Meteorológica IoT com ESP32, Display ST7735S e Publicação via MQTT


Introdução
A coleta e monitoramento de dados meteorológicos em tempo real é fundamental para aplicações agrícolas, ambientais e acadêmicas. No entanto, muitas soluções disponíveis são caras e pouco acessíveis.
Este projeto propõe o desenvolvimento de uma estação meteorológica de baixo custo e conectada à Internet, capaz de medir temperatura, umidade, luminosidade, intensidade de chuva e temperatura ambiente com sensores de fácil aquisição. Os dados são exibidos localmente em um display LCD e enviados para um servidor MQTT, permitindo acesso remoto

Desenvolvimento
A solução foi implementada utilizando o microcontrolador ESP32, programado com ESP-IDF e executando o FreeRTOS.
Sensores do projeto: DHT11 (temperatura e umidade), LDR (luminosidade) ,Sensor de chuva (analógico) e  KY-028 (temperatura com termistor ).
A exibição local utilizando um display LCD ST7735S, controlado via protocolo SPI.
Para comunicação com a nuvem, foi implementado um cliente MQTT que publica dados no formato JSON para um broker na rede local. 
O sistema realiza leituras a cada 5 segundos, atualiza a interface gráfica no display e envia os dados para consumo remoto.

Resultados
O sistema foi capaz de:
Coletar e exibir informações meteorológicas em tempo real e exibir no display.
Publicar os dados de forma confiável em um servidor MQTT( Mosquitto ).
Obter precisão suficiente para aplicações acadêmicas e de monitoramento local.
Garantir comunicação estável via Wi-Fi, com reconexão automática em caso de falha.
A interface visual no display permitiu acompanhamento local rápido, enquanto a publicação via MQTT possibilitou integração com outros sistemas e dashboards remotos.

Conclusão
A estação meteorológica desenvolvida atende aos objetivos de fornecer um sistema de baixo custo, confiável para coleta e transmissão de dados climáticos. A arquitetura modular e a escolha de protocolos amplamente utilizados permitem a expansão do projeto, incluindo novos sensores, armazenamento em nuvem, painéis solares e funcionalidades de alerta.O projeto se mostra adequado para uso educacional, pesquisa e aplicações práticas em campo.


Descrição de como executar o projeto:

- abra projeto pelo vscode
- baixar e instalar a extensão ESP-IDF no VS-Code
    segue link de instalação do mesmo: https://github.com/espressif/vscode-esp-idf-extension/blob/master/README.md
- faça toda a montagem dos sensores no Esp32 , Build e grave o codigo no mesmo.
- baixe e instale o app em seu celular o MQTT Broker ou instale o MQTT mosquito via docker
 - 1 - usar APP MQTT BROKER  
    https://play.google.com/store/apps/details?id=in.naveens.mqttbroker&hl=pt_BR
    - veja qual ip foi destinado a ele na rede wifi    
    - configure um usuario e senha para que seja colocado no codigo, procure as informações abaixo no topo do codigo e as altere.
        #define WIFI_SSID         "Sua WI-Fi"                   // Nome da sua rede Wi-Fi
        #define WIFI_PASS         "Senha da WIFI"               // Senha da sua rede Wi-Fi
        #define MQTT_BROKER_URI   "mqtt://IP_DO_MQTT_Broker:1883"     // Endereço do seu broker MQTT
        #define MQTT_USER         "usuario"                   // Usuário do broker MQTT
        #define MQTT_PASS         "senha"                   // Senha do broker MQTT
    Obs: caso va usar o celular como ponto de acesso o MQTT broker não funciona adequadamente
 - 2 - Usar MQTT Mosquitto
 '  3. Instalar no Windows
        1 - Baixe o instalador oficial:
            https://mosquitto.org/download/

        Instale o Mosquitto (marque a opção para instalar como serviço).
        Após a instalação, o Mosquitto pode ser iniciado pelo Serviços do Windows ou pelo Prompt de Comando:
        mosquitto
        Para testar, use também os clientes:
        Abrir um terminal para subscriber:
        mosquitto_sub -h localhost -t test
        Abrir outro terminal para publisher:
        mosquitto_pub -h localhost -t test -m "Teste MQTT no Windows!"

        2 -  Instalar com Docker (opcional)
        Se preferir rodar isolado:
        docker run -it -p 1883:1883 -p 9001:9001 eclipse-mosquitto  
        
         
- baixe e instale o app em seu celular IoT MQTT Panel
    https://play.google.com/store/apps/details?id=snr.lab.iotmqttpanel.prod&hl=pt_BR
    - Faça a conficuração do servidor MQTT colocando os dados pertinentes(ip, porta , usuario e senha)
    - crie o dashboard no app colocando os dados a serem recebidos em formato " Payload is JSON Data " e acrescente o dado que sera recebido em JsonPath for subscribe:  temperatura, umidade, ky028, luminosidade ou chuva
    - acrescente na opção tropic a informação a seguir:   /ifpe/ads/embarcados/esp32/station/data 
    - escolha a opção de template que melhor lhe agradar seja grafico ou apenas exibir o valor desejado

Notas: caso prefira usar um outro MQTT recomendo ultilidar o Mosquitto via docker. 



