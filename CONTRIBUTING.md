# Contribuindo com o VitalKare

## Convenções de Commit (Conventional Commits)

```
<tipo>(<escopo>): <descrição curta>

[corpo opcional]

[rodapé opcional]
```

### Tipos permitidos

| Tipo       | Quando usar                                |
|------------|--------------------------------------------|
| `feat`     | Nova funcionalidade                        |
| `fix`      | Correção de bug                            |
| `docs`     | Documentação apenas                        |
| `refactor` | Refatoração sem mudança de comportamento   |
| `test`     | Adição ou correção de testes               |
| `chore`    | Tarefas de build, CI, dependências         |
| `perf`     | Melhoria de performance                    |
| `ci`       | Mudanças em pipelines de CI/CD             |

### Escopos sugeridos

`firmware`, `backend`, `dashboard`, `mqtt`, `docker`, `docs`, `ci`

### Exemplos

```
feat(firmware): adiciona driver HDC1080 com leitura I2C
fix(backend): corrige vazamento de sessão no get_db
docs(readme): atualiza quick start para Docker Compose v2
chore(dashboard): atualiza dependências npm
```

---

## Estratégia de Branches

```
main          → código estável, pronto para produção
develop       → integração de features
feature/<nome>→ nova funcionalidade
fix/<nome>    → correção de bug
```

### Fluxo de trabalho

1. Crie uma branch a partir de `develop`:
   ```bash
   git checkout -b feature/minha-feature develop
   ```
2. Faça commits seguindo as convenções acima.
3. Abra um Pull Request para `develop`.
4. Solicite revisão de pelo menos 1 colaborador.
5. Após aprovação, faça merge com `--squash` ou `--rebase`.

---

## Processo de PR

- Título do PR deve seguir o formato de Conventional Commits.
- Descreva o **que** foi feito e o **porquê**.
- Inclua prints de tela quando houver mudanças visuais no dashboard.
- Para firmware: informe resultado de `idf.py build` e teste em hardware real.
- PRs para `main` requerem aprovação do maintainer.

---

## Ambiente de Desenvolvimento

```bash
# Backend
cd backend && pip install -r requirements.txt
uvicorn app.main:app --reload

# Dashboard
cd dashboard && npm install && npm run dev

# Firmware
cd firmware && idf.py build
```
