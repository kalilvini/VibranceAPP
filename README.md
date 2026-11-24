# CS2 Vibrance & Sharpness Utility

Utilitário ultra-leve para Windows focado no **Counter-Strike 2 (`cs2.exe`)** e placas de vídeo **NVIDIA GeForce** (testado e otimizado para a arquitetura Ampere, como a **GeForce RTX 3060 Ti**).

---

## ⚡ Destaques e Diferenciais
1. **Ultra Leve:**
   - Consumo de memória RAM: **~1.5 MB a 3 MB**.
   - Consumo de CPU: **0.0%** (intervalo de verificação de 500 ms via polling leve do Windows).
   - Tamanho do executável compilado: **214 KB** (binário nativo C++ x64 compilado sem dependências de runtime).
2. **100% VAC Safe (Seguro contra Anti-Cheat):**
   - Não injeta DLLs nem toca na memória do processo `cs2.exe`.
   - Utiliza exclusivamente as funções oficiais do driver gráfico da NVIDIA (`nvapi64.dll`).
3. **Controle Dinâmico de Saturação e Nitidez:**
   - Ao abrir ou alternar para a janela do CS2: aplica imediatamente a saturação configurada (ex: **100%**) e o nível de nitidez desejado.
   - Ao minimizar ou fechar o CS2: restaura automaticamente a saturação padrão do Windows (**50%**) e desativa a nitidez adicional.
4. **Detecção Inteligente de Janela / Tela Cheia:**
   - Permite escolher se o perfil deve ser ativado apenas em tela cheia exclusiva ou também em janelas sem bordas / modo janela.
5. **Bandeja do Sistema (System Tray):**
   - Roda silenciosamente minimizado próximo ao relógio do Windows.
   - Menu com botão direito para alternar valores de saturação (100%, 85%, 75%, 50%), nitidez, modo janela e autostart.
6. **Inicialização com o Windows:**
   - Pode ser ativado diretamente pelo menu do ícone na bandeja ou executando `install_autostart.bat`.

---

## 📁 Estrutura de Arquivos

```
CS2Vibrance/
├── CS2Vibrance.exe          # Executável nativo final (pronto para uso direto)
├── config.ini               # Arquivo de configuração padrão
├── vibrance_sharpness.py    # Versão alternativa em script Python
├── install_autostart.bat    # Script para ativar início automático com o Windows
├── uninstall_autostart.bat  # Script para remover do início do Windows
└── src/
    └── main.cpp             # Código-fonte completo em C++ Win32 / NVAPI
```

---

## 🚀 Como Usar

### Opção 1: Executável Nativo (`CS2Vibrance.exe`) - Recomendado
1. Copie a pasta `CS2Vibrance` para o diretório de sua preferência (ex: `C:\Ferramentas\CS2Vibrance` ou `%LOCALAPPDATA%\CS2Vibrance`).
2. Dê um duplo clique em `CS2Vibrance.exe`.
3. O ícone aparecerá instantaneamente na bandeja do sistema (perto do relógio).
4. Clique com o botão direito no ícone para ajustar as opções ou ligar o autostart.

### Opção 2: Script Python (`vibrance_sharpness.py`)
Caso queira executar via interpretador Python:
```bash
python vibrance_sharpness.py
```
*(Nota: não requer bibliotecas de terceiros além do Python nativo com `ctypes` e driver NVIDIA instalado).*

---

## ⚙️ Configurações (`config.ini`)
O arquivo é gerado automaticamente em `%APPDATA%\CS2Vibrance\config.ini`:

```ini
[Settings]
WindowsVibrance=50       ; Saturação normal da área de trabalho (padrão 50%)
CS2Vibrance=100          ; Saturação no CS2 (recomendado 100%)
SharpnessPercent=0       ; Nitidez (0 desativado, ou 1 a 100)
ApplyInWindowed=1        ; 1 = Ativa em modo janela / sem bordas | 0 = Somente tela cheia
PollingIntervalMs=500    ; Intervalo de verificação em milissegundos
TargetProcess=cs2.exe    ; Processo monitorado
```

---

## 🛠️ Recompilação do Código C++ (Opcional)
Caso queira modificar o código-fonte em `src/main.cpp`:

**Via MinGW-w64 (GCC):**
```bash
x86_64-w64-mingw32-g++ -O2 -mwindows -s -static -static-libgcc -static-libstdc++ src/main.cpp -o bin/CS2Vibrance.exe -lkernel32 -luser32 -lshell32 -ladvapi32
```
