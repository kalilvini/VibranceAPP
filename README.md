# VibranceApp - Gerenciador Inteligente de Saturação & Nitidez para Jogos

Um utilitário visual moderno, didático e **ultra-leve** para Windows e placas de vídeo **NVIDIA GeForce** (otimizado para placas como a **GeForce RTX 3060 Ti** e compatíveis).

Diferente de ferramentas legadas que rodavam apenas na bandeja ou funcionavam para um único jogo, o **VibranceApp v2.1** conta com um painel único integrado em Dark Mode, salvamento automático instantâneo, detecção visual de janela ativa em tempo real e 100% de estabilidade.

---

## ⚡ Destaques de Performance e Arquitetura

1. **Impacto Zero no Desempenho dos Jogos:**
   - Consumo de CPU: **0.0%** (intervalo passivo de detecção de foco).
   - Consumo de memória RAM: **~2.5 MB a 4 MB**.
   - Tamanho do binário nativo compilado: **~260 KB** (executável C++ puro, sem instalador pesado e sem depender de Electron ou runtimes gigantescos).
2. **100% Anti-Cheat Safe (VAC, Vanguard, Easy Anti-Cheat, BattlEye):**
   - **Zero Injeção:** Não injeta código ou DLLs dentro dos processos de jogos.
   - Comunica-se exclusivamente com a API oficial do driver NVIDIA (`nvapi64.dll`).
3. **Painel Unificado & Sem Complicações:**
   - Tudo em uma única tela: configure o Vibrance da Área de Trabalho (Windows) e o Vibrance individual de cada jogo.
   - **Salvamento Automático Instantâneo:** Ao mexer no slider ou adicionar jogos, as configurações são salvas na hora.
   - **Monitoramento Visual em Tempo Real:** Mostra na tela o processo em primeiro plano (`Janela Ativa: [ cs2.exe ]`) e a saturação ativa no monitor.
4. **Detecção Resiliente com Dupla Camada (Anti-Cheat / Elevados):**
   - Utiliza `QueryFullProcessImageNameW` com fallback automático para `CreateToolhelp32Snapshot`, garantindo detecção precisa mesmo para jogos que rodam com anti-cheat ou privilégios de Administrador.
5. **Bandeja do Sistema (System Tray):**
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

## 📖 Guia Didático: Entendendo as Opções e Funcionamento

* **O que é Digital Vibrance e por que afeta a tela toda?**
  O *Digital Vibrance* é um ajuste direto nos canais de hardware (LUT) da placa de vídeo NVIDIA antes de enviar o sinal para o monitor. Não existe como alterar a saturação física da tela apenas dentro da moldura de uma janela sem injetar código na memória do jogo (o que causaria banimento por anti-cheats como VAC e Vanguard). Por isso, enquanto o jogo estiver em foco na tela, a placa de vídeo opera com a saturação configurada; ao dar `Alt+Tab` ou fechar o jogo, o VibranceApp restaura instantaneamente os 50% normais da Área de Trabalho.

* **Por que a opção de Nitidez foi removida?**
  A NVIDIA descontinuou o endpoint de nitidez direta (`NvAPI_OverlaySharpness`) nos drivers modernos das séries RTX 30 e 40 (como a RTX 3060 Ti). Como a chamada não surtia mais efeito nesses drivers mais recentes, a opção foi removida para garantir que o programa contenha 100% de controles funcionais, sem nenhum slider placebo.

* **Como funciona a troca automática?**
  O aplicativo monitora passivamente qual janela está em primeiro plano a cada 300ms. Se for um jogo cadastrado (ex: `cs2.exe`), ativa o perfil correspondente. Caso contrário, mantém o padrão da Área de Trabalho. O badge `Janela Ativa Detectada` no topo da janela indica exatamente o que está em foco em tempo real.

---

## 🛠️ Recompilação do Código C++ (Opcional)

Se você quiser compilar por conta própria usando o MinGW-w64:

```bash
x86_64-w64-mingw32-g++ -O2 -mwindows -s -static -static-libgcc -static-libstdc++ src/main.cpp -o VibranceApp.exe -lkernel32 -luser32 -lshell32 -ladvapi32 -lgdi32 -lcomctl32 -luxtheme -ldwmapi -lcomdlg32
```
