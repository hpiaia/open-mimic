# DruML Studio: análise pública do pipeline de modelo e treinamento

Data: 2026-10-26. Fonte: bundle JavaScript servido publicamente em
`https://studio.druml.ai` (Vite/React PWA hospedada no GitHub Pages atrás do
Cloudflare), endpoints públicos de leitura de `https://api.druml.ai`
(`/releases`, `/training-vocab`, `/training/classifier-labels`, `/health`,
`/devices/settings/distribution`) e resenhas públicas. O firmware
(`DruML-v1.0.193.1631_enc.bin`) é distribuído cifrado e não foi analisado.
O `robots.txt` da Studio declara `Content-Signal: ai-train=no, use=reference`:
este documento é referência para interoperabilidade e entendimento de projeto;
não copiar código nem usar o material para treinar modelos.

Tudo abaixo que vem do bundle é **observado**; o que vem de texto de ajuda ou
release notes é **declarado pelo fabricante**; marcações de "inferência" são
nossas.

## Produto e hardware (declarado)

- DruML S1: caixa de 14" com processamento dentro da caixa, USB MIDI class
  compliant, DIN MIDI, MIDI 2.0. Fabricante aparece no MIDI como "SOUP LABS".
- Front-end "Texas Instruments multi-gain", sensor capacitivo de toque,
  amostragem 33 kHz, "inferência neural ~120 µs", pico-para-MIDI típico 2,8 ms,
  pior caso 5 ms, flam mínimo 4,2 ms.
- Parâmetros de ganho de amplificador por sensor: **center, rim, edge, front**
  (quatro sensores piezo na caixa; firmware de pad trata layouts de 3 ou 4
  canais). Ganhos são "ajustados de fábrica" e o texto avisa que mudá-los afeta
  detecção, não só volume.

## Pipeline de inferência no dispositivo (observado no bundle)

Dois estágios, ambos redes neurais rodando no próprio drum:

1. **Peak/segmentation model** ("Minimum Peak Confidence", parâmetro 0..1):
   decide se houve uma batida real antes de classificar. Release notes falam em
   "segmentation model retraining", "peak autoregression" (usa compromissos
   passados para decidir o próximo) e "reduced segmentation suppression".
2. **Strike-type classifier**: softmax sobre 6 classes para caixa
   (`noise, rim, center, muted, cross-stick, rimshot`) ou 5 para prato
   (`noise, edge, bow, splash, bell`). Cada classe tem um "Min Confidence"
   próprio (parâmetros 21–28; a distribuição de frota mostra center em 0,0,
   noise ~0,7, rimshot ~0,6). Um segundo output auxiliar entrega a **posição
   contínua** centro→borda (0..1), usada no CC 16.

Observado nos comandos SysEx de teste (`CLS_INFER` 88–93): a rede é **int8**
com escala/zero-point por saída, entrada em float32 enviada pelo host, saídas
`out1Size` (classes) + `out2Size` (auxiliar). Build 1631 declara
"pizza mode has access to full neural backbone now": há um backbone
compartilhado e cabeças (heads) separadas.

### Vetor de entrada do classificador (observado)

Layout do payload de treino, função `kw`/`Lj` do bundle, por amostra:

| Bloco | Tamanho | Conteúdo |
| --- | ---: | --- |
| handcrafted | 3 + ar(n) | features manuais (as 3 primeiras são picos por sensor usados na UI) |
| waveform | n × 128 | 128 amostras por sensor (3 ou 4 sensores), janela de ~4 ms a 33 kHz |
| hw_embedding | 32 | **fingerprint de hardware** do dispositivo |
| touch_snapshot | 32 | série temporal do sensor capacitivo (+ 31 diferenças + flag, em modelos com toque) |
| edge block | 16 (+16 +12) | pico/energia/posição de pico por sensor, sin/cos/confiança do ângulo, features "thr10" |

Inferência: `inputLen` reportado pelo firmware deve bater com o
`input_len` do modelo gerado no servidor, senão a Studio recusa ("Model
mismatch ... from a different build"). Ou seja, o contrato de entrada é
versionado por build de firmware, como o nosso `[1,69]`.

### Hardware fingerprint (observado + declarado)

O "Fingerprint Wizard" pede para tocar por toda a pele com dinâmicas variadas;
o dispositivo calcula um vetor de 32 floats e o salva. Esse vetor entra como
parte da entrada do classificador em toda inferência e é enviado ao backend
junto com os ganhos de canal (`/calibration/upload`). A Studio mostra
"FLEET MATCH": percentil de quão parecido o seu fingerprint é com os demais
dispositivos no mesmo firmware. Release notes: "Increased capacity of hardware
fingerprint to allow more aggressive size/chassis specific learning";
"mixture-of-experts to allow strike classification to more aggressively adapt
to unique hardware"; "Peak model now has a richer hardware representation".

Inferência nossa: é um modelo condicional ao hardware. Em vez de um modelo
genérico que precisa ser invariante a ganho e acoplamento entre sensores, o
modelo recebe uma descrição do pad e aprende a condicionar a decisão nela, com
um MoE para especializar por classe de hardware. Vocabulário de treino inclui
tamanho (8"–16"), chassi (`default`, `basket_fdm`) e distância do sensor
(`regular`, +1, -1), coletados como metadados de cada sessão.

## Treinamento (observado + declarado)

Três mecanismos distintos, com níveis diferentes de cloud:

1. **Fleet model (fábrica)**: treinado no backend com amostras enviadas por
   todos os usuários ("merged into the shared fleet dataset that future factory
   models are trained on"). Release notes mostram retreinos a cada poucos dias.
   Notas de 1576: ruído "vetado entre dispositivos" com LODO (leave-one-device-
   out) e allow-list por amostra; métricas reportadas em int8.
2. **Fine-tune pessoal (cloud)**: a Studio grava ~15 batidas por zona em modo
   Train (MIDI silenciado), envia para `/training/upload` em lotes de 25, o
   usuário revisa/relabela/deleta, dispara `/training/fine-tune` e baixa um
   blob (`/training/fine-tune/blob`) que é gravado no drum via SysEx
   (comandos 66/67/68, chunks de 7-em-8 bits, commit + reboot). O modelo de
   fábrica fica como fallback. Texto declara "episodic learning in fine-tuning
   which should require fewer samples", "rarely takes more than 3,000 samples".
   A Studio pontua cada amostra com o fleet model, com o fine-tune no servidor
   e **com o próprio drum** (roda a amostra no dispositivo via `CLS_INFER`).
3. **Treino no navegador (sem cloud)**, em Web Workers com código próprio
   (sem TensorFlow.js):
   - `angleTrainWorker`: MLP ReLU (entradas 16/25/28/69/78 → [32,32] ou
     [48×4] → 2) que regride **sin/cos do ângulo** da batida ("pizza mode"),
     Adam com cosine schedule, ruído gaussiano 0,35σ nas entradas, L2 1e-3,
     dropout de blocos de features (zera 3 features de classe 30% das vezes e
     9 de janela 15%). Variante "v9": conv 1D minúscula (4 sensores × 12 taps,
     6 filtros, kernel 3, stride 4) + MLP 35→32→32→2. Também há
     `refitReadout`: **refit só da última camada (ou das duas últimas) de um
     blob já treinado**, 60 épocas, lr 5e-4 cosine, mesmo ruído, para absorver
     amostras novas sem retreinar tudo. O resultado vai ao drum pelos comandos
     100–103 ("angle head") com checksum.
   - `headPatchWorker` ("Tune Hit Detection"): regressão logística com ridge
     sobre os features do detector de pico, ajustando um "patch" (w, b) somado
     ao logit do firmware; usa só correções do usuário (≥ 8 "flips"), com peso
     1,0 para amostras corrigidas e 0,3 para as demais e um termo que mantém o
     patch perto de zero nas não corrigidas. Mostra "antes/depois" sobre as
     batidas usadas e envia as correções ao fabricante.

## Guia de dados declarado (texto do assistente "Tom")

- ~15 batidas por zona com dinâmica variada; nunca gravar na zona errada;
  deletar batidas duplas (são dado ruim e sinal de problema mecânico).
- "100% accuracy is NOT the goal": no limite inferior da dinâmica, rim e edge
  são fisicamente indistinguíveis; a fronteira em volume baixo é uma decisão
  do usuário expressa pelos exemplos, não um fato que o modelo descobre.
- Se o fine-tune não melhora com mais dados, o problema é dado ambíguo; limpar a
  fronteira vale mais que adicionar amostras.
- Falsos disparos por rack/vizinhos se resolvem com amostras de `noise`;
  double/missed triggers são problema do detector de pico, não do classificador.
- Fine-tune é "a correção para batidas mal classificadas; thresholds de
  confiança só empurram fronteiras".
- Segmento máximo da janela para classificar é 4 ms; reduzir baixa latência mas
  custa rimshot e posição; "shorter segments need a model trained with
  early-exit augmentation".

## O que isso muda para o Open Mimic

Comparação direta com o nosso estado atual:

| Tema | DruML | Open Mimic hoje |
| --- | --- | --- |
| Sensores | 3–4 piezos + toque, 33 kHz, janela 4 ms (128 amostras) | 2 piezos, ~11,25 kHz, janela 32 amostras (~2,8 ms) |
| Detecção de batida | rede neural própria com autoregressão | limiar fixo por sensor + holdoff 128 quadros |
| Classificador | backbone + heads, int8, classes com threshold individual, posição contínua | CNN float32 [1,69]→[1,4], gate único 0,65 |
| Condicionamento ao hardware | fingerprint de 32 floats na entrada + MoE | nenhum; modelo genérico |
| Adaptação ao usuário | fine-tune na nuvem, carregado no drum; fallback de fábrica | retreino completo no Mac |
| Adaptação sem nuvem | refit da última camada no navegador | experimento `calibration_head_fit.py` desta semana |
| Correção de detecção | patch logístico sobre o logit do detector | não existe |
| Dados de frota | sim, com vetting LODO | não aplicável |

Decisões que valem a pena copiar como ideia (não como código):

1. **Fingerprint de hardware como entrada**: substitui exatamente o que
   tentamos com matriz/ganho. Um vetor curto calculado na calibração (picos
   por sensor e por articulação, razões rim/pele, ruído) concatenado aos 69
   features e visto no treino com todos os pads. O modelo aprende a
   condicionar; não precisamos acertar a transformação à mão. Testável
   offline hoje com os profiles existentes.
2. **Refit só da cabeça no dispositivo**: já validado parcialmente aqui
   (Aux 2 52→91%, Tom 1 cabeça 86→99%). Combinar com o fingerprint deve
   fechar o Tom 4.
3. **Thresholds por classe** em vez de gate único, e `center` com threshold
   zero.
4. **Patch logístico do detector** para o problema de blips de ruído de
   3 contagens que abrem holdoff: barato, sem retreinar a CNN.
5. **Métrica de fábrica em LODO (leave-one-device-out)** é o que já fazemos
   com folds por pad; manter.
