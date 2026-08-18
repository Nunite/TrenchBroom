const escapeShortcutText = (value) => String(value)
  .replaceAll("&", "&amp;")
  .replaceAll("<", "&lt;")
  .replaceAll(">", "&gt;")
  .replaceAll('"', "&quot;")
  .replaceAll("'", "&#39;");

const key_str = (key) => {
  if (!keys[key]) {
    console.error("Unknown key", key);
    return "";
  }
  return `<span class="shortcut">${escapeShortcutText(keys[key])}</span>`;
};

const fix_ellipsis = (path) => path.replace("…", "...");

const shortcut_str = (shortcuts) => {
  if (!shortcuts || shortcuts.length === 0 || shortcuts[0].key === "") {
    return "";
  }

  const shortcut = shortcuts[0];
  return [...shortcut.modifiers.map(key_str), key_str(shortcut.key)].join("");
};

const menu_item_str = (rawKey) => {
  const key = fix_ellipsis(rawKey);
  const item = menu[key];
  if (!item) {
    console.error("Unknown menu item", key);
    return `<strong>${escapeShortcutText(key)}</strong>`;
  }

  const path = item.path.map(escapeShortcutText).join(" &raquo; ");
  const shortcut = shortcut_str(item.shortcut);
  return `<strong>${path}${shortcut ? ` (${shortcut})` : ""}</strong>`;
};

const action_str = (rawKey) => {
  const key = fix_ellipsis(rawKey);
  if (!actions[key]) {
    console.error("Unknown action", key);
    return "";
  }
  return `<strong>${shortcut_str(actions[key])}</strong>`;
};

const print_key = (key) => document.write(key_str(key));
const print_menu_item = (key) => document.write(menu_item_str(key));
const print_action = (key) => document.write(action_str(key));

(() => {
  const normalize = (value) => value.toLocaleLowerCase().replace(/\s+/g, " ").trim();

  const sectionText = (heading) => {
    const depth = Number(heading.tagName.slice(1));
    const parts = [];
    let current = heading.nextElementSibling;
    while (current) {
      if (/^H[1-6]$/.test(current.tagName) && Number(current.tagName.slice(1)) <= depth) {
        break;
      }
      parts.push(current.textContent || "");
      current = current.nextElementSibling;
    }
    return normalize(parts.join(" "));
  };

  window.addEventListener("DOMContentLoaded", () => {
    const body = document.body;
    const isZh = document.documentElement.lang.startsWith("zh");
    const article = document.getElementById("manual-article");
    const searchInput = document.getElementById("manual-search-input");
    const searchResults = document.getElementById("manual-search-results");
    const navigation = document.getElementById("manual-navigation");
    const navToggle = document.querySelector(".nav-toggle");
    const navScrim = document.querySelector(".navigation-scrim");
    const themeToggle = document.querySelector(".theme-toggle");
    const pageNavigation = document.getElementById("page-navigation-links");
    const currentSectionLabel = document.getElementById("current-section-label");
    const backToTopBtn = document.getElementById("back-to-top");
    const headings = [...article.querySelectorAll("h1[id], h2[id], h3[id]")];
    const topHeadings = [...article.querySelectorAll("h1[id]")];

    // =========================================================================
    // 1. INJECT BLENDER/VITEPRESS CATEGORY CAPTIONS INTO SIDEBAR TOC
    // =========================================================================
    const topUl = navigation ? navigation.querySelector(".manual-navigation-scroll > ul") : null;
    if (topUl) {
      const isApiPage = window.location.pathname.includes("python-api");
      let categories = [];

      if (isApiPage) {
        categories = isZh ? [
          { targetId: "toc-quickstart", title: "1. 架构与快速入门" },
          { targetId: "toc-the_tb2_root_module", title: "2. 核心模块与基元" },
          { targetId: "toc-tb2_document", title: "3. 文档与选区操作" },
          { targetId: "toc-geometry_and_elements", title: "4. 几何对象与图元" },
          { targetId: "toc-tb2_pluginpanel", title: "5. 插件界面与控件" },
          { targetId: "toc-runnable_examples", title: "6. 完整实战示例" }
        ] : [
          { targetId: "toc-quickstart", title: "1. QUICKSTART & CONCEPTS" },
          { targetId: "toc-the_tb2_root_module", title: "2. CORE MODULE & MATH" },
          { targetId: "toc-tb2_document", title: "3. DOCUMENT & SELECTION" },
          { targetId: "toc-geometry_and_elements", title: "4. GEOMETRY & ELEMENTS" },
          { targetId: "toc-tb2_pluginpanel", title: "5. PLUGIN UI & CONTROLS" },
          { targetId: "toc-runnable_examples", title: "6. COMPLETE EXAMPLES" }
        ];
      } else {
        categories = isZh ? [
          { afterIndex: 0, title: "1. 入门与基础" },
          { targetId: "toc-brush_editing_and_creation", title: "2. 几何建模与关卡构建" },
          { targetId: "toc-materials_and_uv", title: "3. 材质、资产与场景组织" },
          { targetId: "toc-preferences_and_compilation", title: "4. 编译、扩展与自动化" },
          { targetId: "toc-getting-involved", fallbackId: "toc-references_and_links", title: "5. 社区与参考" }
        ] : [
          { afterIndex: 0, title: "1. GETTING STARTED" },
          { targetId: "toc-brush_editing_and_creation", title: "2. LEVEL MODELING" },
          { targetId: "toc-materials_and_uv", title: "3. MATERIALS & SCENE" },
          { targetId: "toc-preferences_and_compilation", title: "4. PIPELINE & EXTENSIONS" },
          { targetId: "toc-getting-involved", fallbackId: "toc-references_and_links", title: "5. REFERENCE & LINKS" }
        ];
      }

      categories.forEach(cat => {
        let targetLi = null;
        if (cat.targetId) {
          const a = document.getElementById(cat.targetId);
          if (a) targetLi = a.closest("li");
        }
        if (!targetLi && cat.fallbackId) {
          const a = document.getElementById(cat.fallbackId);
          if (a) targetLi = a.closest("li");
        }
        if (!targetLi && cat.afterIndex === 0) {
          targetLi = topUl.firstElementChild;
        }

        if (targetLi && targetLi.parentElement) {
          const caption = document.createElement("div");
          caption.className = "manual-category-caption";
          caption.textContent = cat.title;
          targetLi.parentElement.insertBefore(caption, targetLi);
        }
      });
    }

    const navLinks = navigation ? [...navigation.querySelectorAll("a[href^='#']")] : [];

    // =========================================================================
    // 2. HEADING ANCHOR LINKS (CLICK TO COPY SECTION URL)
    // =========================================================================
    headings.forEach(heading => {
      if (!heading.id) return;
      const anchor = document.createElement("a");
      anchor.className = "header-anchor";
      anchor.href = `#${heading.id}`;
      anchor.setAttribute("aria-label", "Direct link to heading");
      anchor.textContent = "#";
      anchor.addEventListener("click", (e) => {
        e.preventDefault();
        history.pushState(null, "", `#${heading.id}`);
        heading.scrollIntoView({ behavior: "smooth", block: "start" });
        if (navigator.clipboard) {
          navigator.clipboard.writeText(window.location.href);
        }
      });
      heading.prepend(anchor);
    });

    // =========================================================================
    // 3. INTERACTIVE CODE COPY BUTTONS WITH SVG ICONS
    // =========================================================================
    const copySvg = `<svg viewBox="0 0 24 24" width="13" height="13" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="9" y="9" width="13" height="13" rx="2" ry="2"></rect><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"></path></svg>`;
    const checkSvg = `<svg viewBox="0 0 24 24" width="13" height="13" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"><polyline points="20 6 9 17 4 12"></polyline></svg>`;

    article.querySelectorAll("pre").forEach(pre => {
      const copyBtn = document.createElement("button");
      copyBtn.className = "code-copy-btn";
      copyBtn.type = "button";
      copyBtn.innerHTML = `${copySvg}<span>${isZh ? "复制" : "Copy"}</span>`;
      copyBtn.style.cssText = "position:absolute; top:8px; right:8px; font-size:12px; font-weight:600; padding:4px 9px; border-radius:6px; border:1px solid var(--border-subtle); background:var(--bg-surface); color:var(--text-secondary); cursor:pointer; display:inline-flex; align-items:center; gap:5px; transition:all 0.15s ease; box-shadow:var(--shadow-sm);";

      copyBtn.addEventListener("click", () => {
        const text = pre.querySelector("code")?.textContent || pre.textContent;
        navigator.clipboard.writeText(text).then(() => {
          copyBtn.innerHTML = `${checkSvg}<span>${isZh ? "已复制!" : "Copied!"}</span>`;
          copyBtn.style.color = "var(--brand-primary)";
          copyBtn.style.borderColor = "var(--brand-primary)";
          setTimeout(() => {
            copyBtn.innerHTML = `${copySvg}<span>${isZh ? "复制" : "Copy"}</span>`;
            copyBtn.style.color = "var(--text-secondary)";
            copyBtn.style.borderColor = "var(--border-subtle)";
          }, 2000);
        });
      });
      pre.style.position = "relative";
      pre.appendChild(copyBtn);
    });

    // =========================================================================
    // 4. INTERACTIVE MULTI-ENGINE TAB SWITCHERS
    // =========================================================================
    document.querySelectorAll(".tab-box").forEach(box => {
      const btns = box.querySelectorAll(".tab-btn");
      const panels = box.querySelectorAll(".tab-panel");
      btns.forEach((btn, idx) => {
        btn.addEventListener("click", () => {
          btns.forEach(b => b.classList.remove("active"));
          panels.forEach(p => p.classList.remove("active"));
          btn.classList.add("active");
          if (panels[idx]) panels[idx].classList.add("active");
        });
      });
    });

    // =========================================================================
    // 5. BOTTOM PREV / NEXT CHAPTER DUAL-CARD PAGINATION
    // =========================================================================
    let paginationBar = document.querySelector(".pagination-bar");
    if (!paginationBar) {
      paginationBar = document.createElement("div");
      paginationBar.className = "pagination-bar";
      article.appendChild(paginationBar);
    }

    const updatePagination = (currentH1) => {
      paginationBar.replaceChildren();
      if (!currentH1) return;
      const idx = topHeadings.indexOf(currentH1);
      if (idx === -1) return;

      const prevH1 = idx > 0 ? topHeadings[idx - 1] : null;
      const nextH1 = idx < topHeadings.length - 1 ? topHeadings[idx + 1] : null;

      if (prevH1) {
        const prevCard = document.createElement("a");
        prevCard.className = "pag-card prev";
        prevCard.href = `#${prevH1.id}`;
        prevCard.innerHTML = `<span class="pag-label">${isZh ? "⬅️ 上一章" : "⬅️ PREVIOUS"}</span><span class="pag-title">${escapeShortcutText(prevH1.textContent.replace(/^#\s*/, "").trim())}</span>`;
        paginationBar.appendChild(prevCard);
      } else {
        const placeholder = document.createElement("div");
        paginationBar.appendChild(placeholder);
      }

      if (nextH1) {
        const nextCard = document.createElement("a");
        nextCard.className = "pag-card next";
        nextCard.href = `#${nextH1.id}`;
        nextCard.innerHTML = `<span class="pag-label">${isZh ? "下一章 ➡️" : "NEXT ➡️"}</span><span class="pag-title">${escapeShortcutText(nextH1.textContent.replace(/^#\s*/, "").trim())}</span>`;
        paginationBar.appendChild(nextCard);
      }
    };

    // =========================================================================
    // 6. BACK TO TOP BUTTON LOGIC
    // =========================================================================
    if (backToTopBtn) {
      window.addEventListener("scroll", () => {
        if (window.scrollY > 300) {
          backToTopBtn.removeAttribute("hidden");
        } else {
          backToTopBtn.setAttribute("hidden", "");
        }
      }, { passive: true });

      backToTopBtn.addEventListener("click", () => {
        window.scrollTo({ top: 0, behavior: "smooth" });
      });
    }

    // =========================================================================
    // 7. SEARCH SYSTEM
    // =========================================================================
    const searchIndex = headings.map((heading) => ({
      id: heading.id,
      title: heading.textContent.replace(/^#\s*/, "").trim(),
      text: sectionText(heading),
    }));
    let activeResult = -1;

    const closeSearch = () => {
      if (searchResults) {
        searchResults.hidden = true;
        activeResult = -1;
      }
      if (searchInput) {
        searchInput.setAttribute("aria-expanded", "false");
      }
    };

    const selectResult = (index) => {
      if (!searchResults) return;
      const resultButtons = [...searchResults.querySelectorAll(".search-result")];
      if (resultButtons.length === 0) {
        activeResult = -1;
        return;
      }
      activeResult = (index + resultButtons.length) % resultButtons.length;
      resultButtons.forEach((button, buttonIndex) => {
        button.setAttribute("aria-selected", String(buttonIndex === activeResult));
      });
      resultButtons[activeResult].scrollIntoView({ block: "nearest" });
    };

    const openSection = (id) => {
      closeSearch();
      if (searchInput) searchInput.blur();
      history.pushState(null, "", `#${id}`);
      const heading = document.getElementById(id);
      if (heading) {
        heading.scrollIntoView({ block: "start" });
        heading.focus({ preventScroll: true });
      }
    };

    const renderSearchResults = () => {
      if (!searchInput || !searchResults) return;
      const query = normalize(searchInput.value);
      searchResults.replaceChildren();
      activeResult = -1;

      if (!query) {
        closeSearch();
        return;
      }

      const terms = query.split(" ");
      const matches = searchIndex
        .filter((entry) => terms.every((term) => normalize(`${entry.title} ${entry.text}`).includes(term)))
        .slice(0, 20);

      if (matches.length === 0) {
        const message = document.createElement("p");
        message.className = "search-message";
        message.style.cssText = "padding:12px; font-size:13px; color:var(--text-muted);";
        message.textContent = body.dataset.searchNoResults || "No matching results";
        searchResults.append(message);
      } else {
        matches.forEach((entry) => {
          const button = document.createElement("button");
          const title = document.createElement("strong");
          const context = document.createElement("span");
          button.type = "button";
          button.className = "search-result";
          button.style.cssText = "display:flex; flex-direction:column; width:100%; text-align:left; padding:8px 12px; border:none; background:transparent; cursor:pointer; border-radius:6px; margin-bottom:2px; transition:background 0.15s ease;";
          button.setAttribute("role", "option");
          button.setAttribute("aria-selected", "false");
          title.textContent = entry.title;
          title.style.cssText = "font-size:13.5px; color:var(--brand-primary); margin-bottom:2px;";
          context.textContent = entry.text.slice(0, 140) || body.dataset.searchEmpty;
          context.style.cssText = "font-size:12px; color:var(--text-muted); line-height:1.45;";
          button.append(title, context);
          button.addEventListener("click", () => openSection(entry.id));
          button.addEventListener("mouseenter", () => {
            button.style.background = "var(--bg-hover)";
          });
          button.addEventListener("mouseleave", () => {
            button.style.background = "transparent";
          });
          searchResults.append(button);
        });
      }

      searchResults.hidden = false;
      searchInput.setAttribute("aria-expanded", "true");
    };

    if (searchInput) {
      searchInput.addEventListener("input", renderSearchResults);
      searchInput.addEventListener("keydown", (event) => {
        if (event.key === "ArrowDown" || event.key === "ArrowUp") {
          event.preventDefault();
          const delta = event.key === "ArrowDown" ? 1 : -1;
          selectResult(activeResult < 0 ? (delta > 0 ? 0 : -1) : activeResult + delta);
        } else if (event.key === "Enter") {
          const resultButtons = searchResults.querySelectorAll(".search-result");
          if (resultButtons.length > 0) {
            event.preventDefault();
            resultButtons[activeResult >= 0 ? activeResult : 0].click();
          }
        } else if (event.key === "Escape") {
          closeSearch();
          searchInput.blur();
        }
      });
    }

    document.addEventListener("keydown", (event) => {
      if ((event.ctrlKey || event.metaKey) && event.key.toLocaleLowerCase() === "k") {
        event.preventDefault();
        if (searchInput) {
          searchInput.focus();
          searchInput.select();
        }
      } else if (event.key === "Escape") {
        closeSearch();
      }
    });

    document.addEventListener("click", (event) => {
      if (!event.target.closest(".sidebar-search-wrap") && !event.target.closest(".manual-search-wrap")) {
        closeSearch();
      }
    });

    // =========================================================================
    // 8. NAVIGATION DRAWER & THEMES
    // =========================================================================
    if (navigation && navToggle && navScrim) {
      const setNavigationOpen = (open) => {
        navigation.classList.toggle("open", open);
        navToggle.setAttribute("aria-expanded", String(open));
        navScrim.hidden = !open;
      };
      navToggle.addEventListener("click", () => setNavigationOpen(!navigation.classList.contains("open")));
      navScrim.addEventListener("click", () => setNavigationOpen(false));
      navLinks.forEach((link) => link.addEventListener("click", () => setNavigationOpen(false)));
    }

    const storedTheme = localStorage.getItem("tb-manual-theme");
    if (storedTheme === "light" || storedTheme === "dark") {
      document.documentElement.dataset.theme = storedTheme;
    }
    if (themeToggle) {
      themeToggle.addEventListener("click", () => {
        const effectiveDark = document.documentElement.dataset.theme === "dark"
          || (!document.documentElement.dataset.theme && matchMedia("(prefers-color-scheme: dark)").matches);
        const nextTheme = effectiveDark ? "light" : "dark";
        document.documentElement.dataset.theme = nextTheme;
        localStorage.setItem("tb-manual-theme", nextTheme);
      });
    }

    const updateAlternateLanguageLinks = () => {
      document.querySelectorAll(".language-switcher a:not([aria-current])").forEach((link) => {
        const baseHref = link.getAttribute("href").split("#")[0];
        link.href = `${baseHref}${location.hash}`;
      });
    };
    window.addEventListener("hashchange", updateAlternateLanguageLinks);
    updateAlternateLanguageLinks();

    const topLevelFor = (heading) => {
      let current = heading;
      while (current && current.tagName !== "H1") {
        current = current.previousElementSibling;
      }
      return current;
    };

    const renderPageNavigation = (topLevelHeading) => {
      if (!pageNavigation) return;
      pageNavigation.replaceChildren();
      if (!topLevelHeading) {
        return;
      }
      if (currentSectionLabel) {
        currentSectionLabel.textContent = topLevelHeading.textContent.replace(/^#\s*/, "").trim();
      }
      let current = topLevelHeading.nextElementSibling;
      while (current && current.tagName !== "H1") {
        if ((current.tagName === "H2" || current.tagName === "H3") && current.id) {
          const link = document.createElement("a");
          link.href = `#${current.id}`;
          link.textContent = current.textContent.replace(/^#\s*/, "").trim();
          link.className = current.tagName === "H3" ? "depth-3" : "depth-2";
          pageNavigation.append(link);
        }
        current = current.nextElementSibling;
      }
    };

    const setActiveHeading = (heading) => {
      navLinks.forEach((link) => link.classList.toggle("active", link.hash === `#${heading.id}`));
      const topLevel = topLevelFor(heading);
      if (pageNavigation && pageNavigation.dataset.section !== topLevel?.id) {
        pageNavigation.dataset.section = topLevel?.id || "";
        renderPageNavigation(topLevel);
        updatePagination(topLevel);
      }
      if (pageNavigation) {
        [...pageNavigation.querySelectorAll("a")].forEach((link) => {
          link.classList.toggle("active", link.hash === `#${heading.id}`);
        });
      }
    };

    headings.forEach((heading) => heading.tabIndex = -1);
    const headerHeight = parseInt(getComputedStyle(document.documentElement).getPropertyValue("--header-height"), 10) || 54;
    const observer = new IntersectionObserver((entries) => {
      const visible = entries.filter((entry) => entry.isIntersecting)
        .sort((a, b) => a.boundingClientRect.top - b.boundingClientRect.top);
      if (visible.length > 0) {
        setActiveHeading(visible[0].target);
      }
    }, { rootMargin: `-${headerHeight + 10}px 0px -70% 0px` });
    headings.forEach((heading) => observer.observe(heading));

    const initialHeading = location.hash ? document.getElementById(location.hash.slice(1)) : headings[0];
    if (initialHeading) {
      setActiveHeading(initialHeading);
    }
  });
})();
