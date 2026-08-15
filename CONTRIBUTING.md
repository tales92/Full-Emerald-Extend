# Contribuindo

Obrigado por ajudar o Full Emerald.

## Antes de abrir um PR

1. Abra ou relacione uma issue para mudanças grandes.
2. Mantenha ROM, BIOS, save, states, generated shards e assets sem licença fora
   do Git.
3. Trabalhe em uma branch curta e focada.
4. Preserve o comportamento vanilla quando o recurso estiver desligado.
5. Adicione testes proporcionais ao risco.
6. Rode `ctest --test-dir build --output-on-failure`.
7. Execute `git diff --check` no repositório principal e nos submódulos.

## Bugs

Inclua versão, hash da ROM (nunca a ROM), Windows, CPU/GPU, controle, passos
reproduzíveis e logs sem dados pessoais. Para Multitelas, informe quantidade de
workers, momento da falha e se o modo normal continua funcional.

## Código

- C++20 no produto e runtime; C11 onde o recomp-ui usa C.
- Não edite os shards gerados.
- Evite alocações e exceções em callbacks de frame/hook.
- Use adapters fechados por hash para estruturas específicas do jogo.
- Faça caches serem invalidados em reset e state-load.

## Assets

Todo asset precisa de autor, fonte e licença verificável. “Encontrado na
internet”, “uso livre” sem fonte ou crédito isolado não bastam. Quando houver
dúvida, forneça um importador e mantenha o asset fora do repositório.

Ao contribuir, você declara ter direito de enviar o conteúdo e aceita que ele
permaneça sujeito aos termos documentados em [LEGAL.md](LEGAL.md).
