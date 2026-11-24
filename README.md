# VibranceApp - Gerenciador Inteligente de Saturação & Nitidez para Jogos

Um utilitário visual moderno, didático e **ultra-leve** para Windows e placas de vídeo **NVIDIA GeForce** (otimizado para placas como a **GeForce RTX 3060 Ti** e compatíveis).

Diferente de ferramentas legadas que rodavam apenas na bandeja ou funcionavam para um único jogo, o **VibranceApp v2.0** conta com uma interface gráfica completa em Dark Mode, suporte a múltiplos jogos personalizados, teste ao vivo no monitor e explicações didáticas integradas.

---

## ⚡ Destaques de Performance e Arquitetura

1. **Impacto Zero no Desempenho dos Jogos:**
   - Consumo de CPU: **0.0%** (intervalo passivo de detecção de foco).
   - Consumo de memória RAM: **~2.5 MB a 4 MB**.
   - Tamanho do binário nativo compilado: **259 KB** (executável C++ puro, sem instalador pesado e sem depender de Electron ou runtimes gigantescos).
2. **100% Anti-Cheat Safe (VAC, Vanguard, Easy Anti-Cheat, BattlEye):**
   - **Zero Injeção:** Não injeta código ou DLLs dentro dos processos de jogos.
   - Comunica-se exclusivamente com a API oficial do driver NVIDIA (`nvapi64.dll`).
3. **Gerenciador Multi-Jogos Completo:**
   - Já vem pré-configurado com os principais títulos: **Counter-Strike 2, Valorant, Apex Legends, Warzone / Call of Duty, Fortnite, Overwatch 2, Rainbow Six Siege**.
   - Permite adicionar **qualquer jogo** do seu computador:
     - Botão **"🔍 Detectar Abertos"**: lista os processos ativos no Windows para você selecionar o jogo com 1 clique.
     - Botão **"📁 Procurar .exe"**: selecione o arquivo executável direto pelo Explorador de Arquivos.
4. **Teste Rápido / Ao Vivo (Live Preview):**
   - Aba exclusiva com sliders de saturação (0% a 100%) e nitidez para testar e ver a imagem mudando em tempo real na tela antes mesmo de abrir os jogos.
   - Botão para restaurar instantaneamente para o padrão da Área de Trabalho (50%).
5. **Modo Janela / Sem Bordas vs Tela Cheia Exclusiva:**
   - Ajustável individualmente por jogo.
6. **Bandeja do Sistema (System Tray):**
   - O botão de fechar (X) minimiza automaticamente para a bandeja próxima ao relógio, continuando ativo de forma silenciosa.
   - Duplo clique no ícone reabre a janela principal instantaneamente.
   - Opção para iniciar automaticamente com o Windows (gravado no Registro).

---

## 📁 Estrutura do Repositório

```text
VibranceAPP/
├── VibranceApp.exe          # Executável nativo C++ com interface completa (259 KB)
├── CS2Vibrance.exe          # Cópia de conveniência com o mesmo binário GUI
├── vibrance_gui.py          # Versão alternativa em Python com interface Tkinter
├── vibrance_sharpness.py    # Versão em script leve de linha de comando
├── install_autostart.bat    # Script para ativar início automático com o Windows
├── uninstall_autostart.bat  # Script para remover do início do Windows
├── README.md                # Documentação e manual de uso
└── src/
    └── main.cpp             # Código-fonte completo em C++ Win32 nativo
```

---

## 🚀 Como Executar

### Opção 1: Executável Nativo Windows (`VibranceApp.exe`) - Recomendado
1. Baixe o `VibranceApp.exe` da página de [Releases](https://github.com/kalilvini/VibranceAPP/releases).
2. Execute o arquivo. A janela do painel abrirá instantaneamente em modo escuro.
3. Personalize a saturação dos seus jogos preferidos ou teste ao vivo na aba de teste.
4. Ao fechar a janela, o aplicativo continua ativo minimizado na bandeja.

### Opção 2: Interface em Python (`vibrance_gui.py`)
Caso queira rodar diretamente via código:
```bash
python vibrance_gui.py
```
*(Requer apenas Python padrão do Windows com `ctypes` e `tkinter`).*

---

## 📖 Guia Didático: Entendendo as Opções

* **O que é Digital Vibrance?**
  Ajusta o processamento cromático da placa de vídeo antes de enviar os quadros para o monitor. Em jogos de tiro, aumenta a separação entre cores frias do cenário e cores quentes das silhuetas dos adversários, facilitando a identificação de alvos escondidos em cantos escuros.
* **O que é Nitidez (Image Sharpening)?**
  Acentua o contraste de bordas e texturas distantes. É crucial para quem joga em resoluções esticadas (como 4:3 1280x960), eliminando a sensação de imagem embaçada provocada pelo monitor ao esticar a resolução.
* **Como funciona a troca automática?**
  O aplicativo verifica qual janela está com o foco do usuário no Windows. Ao entrar no jogo, ativa a saturação configurada. Ao sair do jogo ou pressionar `Alt+Tab`, restaura as cores normais (50%) para navegação confortável na internet.

---

## 🛠️ Recompilação do Código C++ (Opcional)

Se você quiser compilar por conta própria usando o MinGW-w64:

```bash
x86_64-w64-mingw32-g++ -O2 -mwindows -s -static -static-libgcc -static-libstdc++ src/main.cpp -o VibranceApp.exe -lkernel32 -luser32 -lshell32 -ladvapi32 -lgdi32 -lcomctl32 -luxtheme -ldwmapi -lcomdlg32
```
