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
    if (!article) return;

    const searchInput = document.getElementById("manual-search-input");
    const searchResults = document.getElementById("manual-search-results");
    const navigation = document.getElementById("manual-navigation");
    const navToggle = document.querySelector(".nav-toggle");
    const navScrim = document.querySelector(".navigation-scrim");
    const themeToggle = document.querySelector(".theme-toggle");
    const pageNavigation = document.getElementById("page-navigation-links");
    const currentSectionLabel = document.getElementById("current-section-label");
    const backToTopBtn = document.getElementById("back-to-top");

    // =========================================================================
    // 1. LAZY IMAGE PERFORMANCE OPTIMIZATION
    // =========================================================================
    article.querySelectorAll("img").forEach(img => {
      if (!img.getAttribute("loading")) img.setAttribute("loading", "lazy");
      if (!img.getAttribute("decoding")) img.setAttribute("decoding", "async");
    });

    // =========================================================================
    // 2. INJECT CATEGORY CAPTIONS INTO SIDEBAR TOC
    // =========================================================================
    const topUl = navigation ? navigation.querySelector(".manual-navigation-scroll > ul") : null;
    if (topUl) {
      const isApiPage = window.location.pathname.includes("python-api");
      let categories = [];

      if (isApiPage) {
        categories = isZh ? [
          { targetId: "toc-quickstart", title: "1. 架构与快速入门" },
          { targetId: "toc-the_trenchbroom_root_module", title: "2. 核心模块与基元" },
          { targetId: "toc-trenchbroom_document", title: "3. 文档与选区操作" },
          { targetId: "toc-geometry_and_elements", title: "4. 几何对象与图元" },
          { targetId: "toc-trenchbroom_pluginpanel", title: "5. 插件界面与控件" },
          { targetId: "toc-runnable_examples", title: "6. 完整实战示例" }
        ] : [
          { targetId: "toc-quickstart", title: "1. QUICKSTART & CONCEPTS" },
          { targetId: "toc-the_trenchbroom_root_module", title: "2. CORE MODULE & MATH" },
          { targetId: "toc-trenchbroom_document", title: "3. DOCUMENT & SELECTION" },
          { targetId: "toc-geometry_and_elements", title: "4. GEOMETRY & ELEMENTS" },
          { targetId: "toc-trenchbroom_pluginpanel", title: "5. PLUGIN UI & CONTROLS" },
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

    const navLinks = navigation ? [...navigation.querySelectorAll("a[href*='#']")] : [];

    // =========================================================================
    // 3. CHAPTER PARTITIONING ENGINE (MODERN MULTI-CHAPTER ARCHITECTURE)
    // =========================================================================
    let paginationBar = document.querySelector(".pagination-bar");
    if (!paginationBar) {
      paginationBar = document.createElement("div");
      paginationBar.className = "pagination-bar";
      article.appendChild(paginationBar);
    }

    const topHeadings = [...article.querySelectorAll("h1[id]")];
    const anchorToChapter = new Map();
    const chapterMap = new Map();
    const chapterList = [];

    if (topHeadings.length > 1) {
      topHeadings.forEach((h1) => {
        const chapterSection = document.createElement("section");
        chapterSection.className = "manual-chapter";
        chapterSection.id = `chapter-${h1.id}`;
        chapterSection.dataset.chapterId = h1.id;
        chapterSection.dataset.chapterTitle = h1.textContent.replace(/^#\s*/, "").trim();

        const nodes = [h1];
        let next = h1.nextElementSibling;
        while (next && next.tagName !== "H1" && !next.classList.contains("pagination-bar") && !next.classList.contains("manual-chapter")) {
          const current = next;
          next = next.nextElementSibling;
          nodes.push(current);
        }

        nodes.forEach(node => chapterSection.appendChild(node));
        chapterMap.set(h1.id, chapterSection);
        chapterList.push({ id: h1.id, title: chapterSection.dataset.chapterTitle, section: chapterSection, h1: h1 });
      });

      chapterList.forEach(item => {
        article.insertBefore(item.section, paginationBar);
        item.section.querySelectorAll("[id]").forEach(el => {
          anchorToChapter.set(el.id, item.id);
        });
      });
    } else if (topHeadings.length === 1) {
      const h1 = topHeadings[0];
      chapterMap.set(h1.id, article);
      chapterList.push({ id: h1.id, title: h1.textContent.replace(/^#\s*/, "").trim(), section: article, h1: h1 });
      article.querySelectorAll("[id]").forEach(el => {
        anchorToChapter.set(el.id, h1.id);
      });
    }

    // =========================================================================
    // 4. HEADING ANCHOR LINKS (#) & COPY TO CLIPBOARD
    // =========================================================================
    const allHeadings = [...article.querySelectorAll("h1[id], h2[id], h3[id]")];
    allHeadings.forEach(heading => {
      if (!heading.id) return;
      const anchor = document.createElement("a");
      anchor.className = "header-anchor";
      anchor.href = `#${heading.id}`;
      anchor.setAttribute("aria-label", "Direct link to heading");
      anchor.textContent = "#";
      heading.prepend(anchor);
    });

    // =========================================================================
    // 5. INTERACTIVE CODE COPY BUTTONS WITH SVG ICONS
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
    // 6. INTERACTIVE MULTI-ENGINE TAB SWITCHERS
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
    // 7. CHAPTER ACTIVATION & SCROLL SYNCHRONIZATION ENGINE
    // =========================================================================
    let currentActiveChapterId = null;
    let isProgrammaticScrolling = false;

    const updateAlternateLanguageLinks = (targetHash) => {
      const hash = targetHash ? `#${targetHash.replace(/^#/, "")}` : location.hash;
      document.querySelectorAll(".language-switcher a:not([aria-current])").forEach((link) => {
        const baseHref = link.getAttribute("href").split("#")[0];
        link.href = `${baseHref}${hash}`;
      });
    };

    const updatePagination = (chapterIndex) => {
      paginationBar.replaceChildren();
      if (chapterIndex < 0 || chapterIndex >= chapterList.length) return;

      const prevItem = chapterIndex > 0 ? chapterList[chapterIndex - 1] : null;
      const nextItem = chapterIndex < chapterList.length - 1 ? chapterList[chapterIndex + 1] : null;

      if (prevItem) {
        const prevCard = document.createElement("a");
        prevCard.className = "pag-card prev";
        prevCard.href = `#${prevItem.id}`;
        prevCard.innerHTML = `<span class="pag-label">${isZh ? "⬅️ 上一章" : "⬅️ PREVIOUS"}</span><span class="pag-title">${escapeShortcutText(prevItem.title)}</span>`;
        prevCard.addEventListener("click", (e) => {
          e.preventDefault();
          activateChapter(prevItem.id, prevItem.id, true);
        });
        paginationBar.appendChild(prevCard);
      } else {
        const placeholder = document.createElement("div");
        paginationBar.appendChild(placeholder);
      }

      if (nextItem) {
        const nextCard = document.createElement("a");
        nextCard.className = "pag-card next";
        nextCard.href = `#${nextItem.id}`;
        nextCard.innerHTML = `<span class="pag-label">${isZh ? "下一章 ➡️" : "NEXT ➡️"}</span><span class="pag-title">${escapeShortcutText(nextItem.title)}</span>`;
        nextCard.addEventListener("click", (e) => {
          e.preventDefault();
          activateChapter(nextItem.id, nextItem.id, true);
        });
        paginationBar.appendChild(nextCard);
      }
    };

    const renderPageNavigation = (chapterSection, activeHeadingId) => {
      if (!pageNavigation) return;
      pageNavigation.replaceChildren();
      if (!chapterSection) return;

      const subHeadings = [...chapterSection.querySelectorAll("h2[id], h3[id]")];
      subHeadings.forEach((heading) => {
        const link = document.createElement("a");
        link.href = `#${heading.id}`;
        link.textContent = heading.textContent.replace(/^#\s*/, "").trim();
        link.className = heading.tagName === "H3" ? "depth-3" : "depth-2";
        if (heading.id === activeHeadingId) {
          link.classList.add("active");
        }
        link.addEventListener("click", (e) => {
          e.preventDefault();
          history.pushState(null, "", `#${heading.id}`);
          updateAlternateLanguageLinks(heading.id);
          isProgrammaticScrolling = true;
          heading.scrollIntoView({ behavior: "smooth", block: "start" });
          setTimeout(() => { isProgrammaticScrolling = false; updateActiveHeadingOnScroll(); }, 400);
          [...pageNavigation.querySelectorAll("a")].forEach((a) => a.classList.toggle("active", a === link));
        });
        pageNavigation.append(link);
      });
    };

    const getActiveHeadingInChapter = (chapterSection) => {
      const headings = [...chapterSection.querySelectorAll("h1[id], h2[id], h3[id]")];
      if (headings.length === 0) return null;

      const headerHeight = parseInt(getComputedStyle(document.documentElement).getPropertyValue("--header-height"), 10) || 54;
      const threshold = headerHeight + 50;

      // Check if at the bottom of the page
      if (window.innerHeight + window.scrollY >= document.documentElement.scrollHeight - 30) {
        return headings[headings.length - 1];
      }

      let active = headings[0];
      for (let i = 0; i < headings.length; i++) {
        const top = headings[i].getBoundingClientRect().top;
        if (top <= threshold) {
          active = headings[i];
        } else {
          break;
        }
      }
      return active;
    };

    const updateActiveHeadingOnScroll = () => {
      if (isProgrammaticScrolling) return;
      const activeChapterItem = chapterList.find(c => c.id === currentActiveChapterId);
      if (!activeChapterItem) return;

      const activeHeading = getActiveHeadingInChapter(activeChapterItem.section);
      if (!activeHeading) return;

      const activeId = activeHeading.id;

      // 1. Synchronize Right Sidebar
      if (pageNavigation) {
        [...pageNavigation.querySelectorAll("a")].forEach((a) => {
          a.classList.toggle("active", a.hash === `#${activeId}`);
        });
      }

      // 2. Synchronize Left Sidebar
      // If activeHeading is H3, find its parent H2
      let targetLeftId = activeId;
      if (activeHeading.tagName === "H3") {
        let prev = activeHeading.previousElementSibling;
        while (prev) {
          if (prev.tagName === "H2" && prev.id) {
            targetLeftId = prev.id;
            break;
          }
          prev = prev.previousElementSibling;
        }
      }

      const allLeftLinks = navigation ? [...navigation.querySelectorAll(".manual-navigation-scroll a")] : [];
      let hasExactMatch = false;

      allLeftLinks.forEach((link) => {
        const linkHash = link.hash ? link.hash.replace(/^#/, "") : "";
        if (linkHash === activeId) {
          link.classList.add("active");
          hasExactMatch = true;
        } else {
          link.classList.remove("active");
        }
      });

      if (!hasExactMatch) {
        allLeftLinks.forEach((link) => {
          const linkHash = link.hash ? link.hash.replace(/^#/, "") : "";
          if (linkHash === targetLeftId || (!targetLeftId && linkHash === currentActiveChapterId)) {
            link.classList.add("active");
          }
        });
      }

      // Mark parent chapter <li> in left sidebar
      if (navigation) {
        [...navigation.querySelectorAll(".manual-navigation-scroll > ul > li")].forEach(li => {
          const chapterLink = li.querySelector(":scope > a");
          const isThisChapter = chapterLink && (chapterLink.hash ? chapterLink.hash.replace(/^#/, "") : "") === currentActiveChapterId;
          li.classList.toggle("current-chapter", Boolean(isThisChapter));
        });

        const activeLeftLink = navigation.querySelector(".manual-navigation-scroll a.active");
        if (activeLeftLink) {
          activeLeftLink.scrollIntoView({ block: "nearest", behavior: "smooth" });
        }
      }

      // 3. Keep URL hash and language switcher updated
      history.replaceState(null, "", `#${activeId}`);
      updateAlternateLanguageLinks(activeId);
    };

    let scrollRaf = null;
    window.addEventListener("scroll", () => {
      if (scrollRaf) return;
      scrollRaf = requestAnimationFrame(() => {
        scrollRaf = null;
        updateActiveHeadingOnScroll();
      });
    }, { passive: true });

    const activateChapter = (targetChapterId, targetAnchorId = null, shouldScroll = true) => {
      let chapterIndex = chapterList.findIndex(c => c.id === targetChapterId);
      if (chapterIndex === -1) {
        chapterIndex = 0;
        targetChapterId = chapterList[0]?.id || "";
      }
      const activeItem = chapterList[chapterIndex];
      if (!activeItem) return;

      currentActiveChapterId = targetChapterId;

      // 1. Toggle chapter visibility
      chapterList.forEach((item, idx) => {
        if (idx === chapterIndex) {
          item.section.classList.add("active");
        } else {
          item.section.classList.remove("active");
        }
      });

      // 2. Update breadcrumbs
      if (currentSectionLabel) {
        currentSectionLabel.textContent = activeItem.title;
      }

      // 3. Update right TOC & bottom pagination
      renderPageNavigation(activeItem.section, targetAnchorId);
      updatePagination(chapterIndex);

      // 4. Update left sidebar active links
      const anchorToMatch = targetAnchorId || targetChapterId;
      const allLeftLinks = navigation ? [...navigation.querySelectorAll(".manual-navigation-scroll a")] : [];
      allLeftLinks.forEach(link => {
        const linkHash = link.hash ? link.hash.replace(/^#/, "") : "";
        link.classList.toggle("active", linkHash === anchorToMatch);
      });

      if (navigation) {
        [...navigation.querySelectorAll(".manual-navigation-scroll > ul > li")].forEach(li => {
          const chapterLink = li.querySelector(":scope > a");
          const isThisChapter = chapterLink && (chapterLink.hash ? chapterLink.hash.replace(/^#/, "") : "") === currentActiveChapterId;
          li.classList.toggle("current-chapter", Boolean(isThisChapter));
        });
      }

      // 5. Update URL state and Language switchers
      const finalHash = targetAnchorId || targetChapterId;
      history.replaceState(null, "", `#${finalHash}`);
      updateAlternateLanguageLinks(finalHash);

      // 6. Handle Scrolling
      if (shouldScroll) {
        if (targetAnchorId && targetAnchorId !== targetChapterId) {
          const targetEl = document.getElementById(targetAnchorId);
          if (targetEl) {
            isProgrammaticScrolling = true;
            targetEl.scrollIntoView({ behavior: "smooth", block: "start" });
            setTimeout(() => { isProgrammaticScrolling = false; updateActiveHeadingOnScroll(); }, 400);
          }
        } else {
          window.scrollTo({ top: 0, behavior: "instant" });
          updateActiveHeadingOnScroll();
        }
      } else {
        updateActiveHeadingOnScroll();
      }
    };

    // =========================================================================
    // 8. GLOBAL LINK CLICK INTERCEPTION (ZERO-LAG CHAPTER ROUTING)
    // =========================================================================
    document.addEventListener("click", (e) => {
      const link = e.target.closest("a");
      if (!link) return;

      const href = link.getAttribute("href");
      if (!href || !href.includes("#")) return;

      if (link.closest(".language-switcher") || link.closest(".theme-toggle") || link.target === "_blank") return;

      const hashMatch = href.match(/#([a-zA-Z0-9_\-]+)/);
      if (hashMatch) {
        const anchorId = hashMatch[1];
        const destChapterId = anchorToChapter.get(anchorId) || (chapterMap.has(anchorId) ? anchorId : null);
        if (destChapterId) {
          e.preventDefault();
          activateChapter(destChapterId, anchorId, true);
          if (navigation && navigation.classList.contains("open")) {
            navigation.classList.remove("open");
            if (navScrim) navScrim.hidden = true;
          }
        }
      }
    });

    // =========================================================================
    // 9. HIGH-PERFORMANCE LAZY SEARCH SYSTEM
    // =========================================================================
    let searchIndex = null;
    const ensureSearchIndex = () => {
      if (searchIndex !== null) return;
      searchIndex = allHeadings.map((heading) => ({
        id: heading.id,
        title: heading.textContent.replace(/^#\s*/, "").trim(),
        text: sectionText(heading),
      }));
    };

    if ("requestIdleCallback" in window) {
      requestIdleCallback(ensureSearchIndex, { timeout: 3000 });
    } else {
      setTimeout(ensureSearchIndex, 1000);
    }

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
      const destChapterId = anchorToChapter.get(id) || id;
      activateChapter(destChapterId, id, true);
    };

    const renderSearchResults = () => {
      if (!searchInput || !searchResults) return;
      ensureSearchIndex();

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
      searchInput.addEventListener("focus", ensureSearchIndex);
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
          ensureSearchIndex();
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
    // 10. NAVIGATION DRAWER & THEMES
    // =========================================================================
    if (navigation && navToggle && navScrim) {
      const setNavigationOpen = (open) => {
        navigation.classList.toggle("open", open);
        navToggle.setAttribute("aria-expanded", String(open));
        navScrim.hidden = !open;
      };
      navToggle.addEventListener("click", () => setNavigationOpen(!navigation.classList.contains("open")));
      navScrim.addEventListener("click", () => setNavigationOpen(false));
    }

    if (themeToggle) {
      themeToggle.addEventListener("click", () => {
        const currentTheme = document.documentElement.dataset.theme ||
          (matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
        const nextTheme = currentTheme === "dark" ? "light" : "dark";
        document.documentElement.dataset.theme = nextTheme;
        try {
          localStorage.setItem("tb-manual-theme", nextTheme);
        } catch (e) {}
      });
    }

    // =========================================================================
    // 11. BACK TO TOP BUTTON LOGIC
    // =========================================================================
    if (backToTopBtn) {
      let bttTimer = null;
      window.addEventListener("scroll", () => {
        if (bttTimer) return;
        bttTimer = setTimeout(() => {
          bttTimer = null;
          if (window.scrollY > 300) {
            backToTopBtn.removeAttribute("hidden");
          } else {
            backToTopBtn.setAttribute("hidden", "");
          }
        }, 100);
      }, { passive: true });

      backToTopBtn.addEventListener("click", () => {
        window.scrollTo({ top: 0, behavior: "smooth" });
      });
    }

    // =========================================================================
    // 12. INITIALIZATION ROUTING (FROM HASH OR DEFAULT FIRST CHAPTER)
    // =========================================================================
    const initialHash = location.hash ? location.hash.replace(/^#/, "") : "";
    let initChapterId = chapterList[0]?.id || "";
    if (initialHash) {
      const foundChapter = anchorToChapter.get(initialHash) || (chapterMap.has(initialHash) ? initialHash : null);
      if (foundChapter) {
        initChapterId = foundChapter;
      }
    }

    activateChapter(initChapterId, initialHash, Boolean(initialHash && initialHash !== initChapterId));

    window.addEventListener("hashchange", () => {
      const newHash = location.hash ? location.hash.replace(/^#/, "") : "";
      if (newHash) {
        const targetChapter = anchorToChapter.get(newHash) || (chapterMap.has(newHash) ? newHash : null);
        if (targetChapter && targetChapter !== currentActiveChapterId) {
          activateChapter(targetChapter, newHash, true);
        }
      }
    });
  });
})();
